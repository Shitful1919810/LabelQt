# 架构说明

LabelQt 是一个独立的 C++/Qt 6 Widgets 应用。它的核心目标是编辑经典 LabelPlus `.txt` 工程，同时支持合并、页面管理、校对与自动化 v2 批处理。基于新 API 的 OCR 与 AI 翻译提供者尚待实现，不内置旧版脚本。

这份文档记录当前代码的职责边界。新增功能前请先确认它应该放在哪一层，避免把新的状态机、文件格式处理或业务规则继续塞回 `MainWindow`。

## 总体分层

```text
src/core       平台无关的数据模型、文件格式、偏好设置、撤销栈
src/services   工程工作流、会话状态、自动化、图片缓存、密钥等应用服务
src/ui         Qt Widgets 界面、模型/委托、画布、对话框和 UI 控制器
scripts        官方/用户自动化脚本，以及官方脚本 SDK
translations   Qt Linguist 翻译源文件
tests          Qt Test 测试
```

基本规则：

- `src/core` 不依赖 Qt Widgets，也不应该知道窗口、菜单、按钮和表格。
- `src/services` 可以使用 QtCore、文件 IO、`QSettings`、`QProcess` 等能力，但不应该依赖具体控件。
- `src/ui` 负责呈现和交互，可以调用核心层和服务层，但不要直接承担文件格式、工程工作流、撤销命令生成和本机状态存储。
- `MainWindow` 是 UI 编排层，不是业务逻辑仓库。新功能如果需要独立状态、异步流程、数据转换或可复用规则，优先放进服务类或专门的 UI 控制器。

当前 `src/services` 按领域分成：

```text
src/services/automation   自动化脚本发现、运行、Python 解析、密钥和操作应用
src/services/preferences  偏好文件的版本基线、写锁与原子保存
src/services/io           图片异步缓存等输入输出设施
src/services/label        标签编辑、导航、复制剪切粘贴规划
src/services/project      工程打开保存、合并、页序调整、来源信息、会话状态
src/services/proofread    校对/工程对比、文本 diff、报告导出和修订元数据
```

当前 `src/ui` 按界面区域分成：

```text
src/ui/automation         自动化菜单、参数窗口、日志窗口和快捷键配置
src/ui/canvas             图像预览画布、标记绘制、临时文本框和图片页显示控制
src/ui/labeltable         右侧标签表格、委托、选择同步和表格列宽辅助
src/ui/preferences        偏好设置窗口和复杂配置子控件
src/ui/project            页面选择、页面顺序调整、工程合并和主界面页控件
src/ui/proofread          校对变更展示窗口
src/ui/search             工程全文查找与替换面板、结果模型
src/ui/widgets            可复用小控件、主题、对话框窗口标志等通用 UI 辅助
```

新增文件时优先落到这些领域子目录。只有确实属于主窗口编排、全局快捷键路由或活动编辑器状态检测的代码，才继续放在 `src/ui` 根目录。

## `src/core`

`src/core` 是工程数据和基础规则层。

- `Label`：单个标签，包含文本、分组、归一化坐标和软删除状态。
- `ProjectIdentityService`：持久化稳定工程/页面/Label/分组 UUID，处理身份校验和复制语义。
- `Project` / `ImageEntry`：当前工程的内存模型。`Project` 持有页面列表、分组、工程路径、注释区等信息；`ImageEntry` 持有图片名、图片路径和该页标签。
- `LabelPlusDocument`：经典 LabelPlus `.txt` 工程格式的解析和保存。格式兼容逻辑应集中在这里，不要在 UI 中拼接工程文本。
- `AtomicFileWriter`：可靠的原子字节写入入口，不负责具体文件格式。工程、HTML 和偏好保存必须校验最终提交成功。
- `AppPreferences`：`preference.json` 的类型化读取、默认值、序列化和诊断警告。`AppPreferencesValues` 是唯一内置默认值来源，随附 JSON 由测试校验一致。偏好缺失、JSON 损坏或类型错误时应回退到内置默认值并返回警告。
- `ApplicationTheme`：内置 Breeze 样式表主题注册。
- `TranslationManager`：发现和加载 `labelqt_*.qm` 翻译资源。
- `UndoStack`：对 Qt `QUndoStack` / `QUndoCommand` 的薄包装，项目内可逆编辑都应通过它进入撤销/重做系统。

新增项目字段时，通常需要同时检查：

- `Label` / `Project`
- `LabelPlusDocument`
- `ProjectTaskService` 的作用域快照与 `AutomationProtocol` 的 v2 JSON 编解码
- 相关 UI 模型和委托
- `tests/LabelTests.cpp`

## `src/services`

`src/services` 放应用服务。它们可以被 UI 调用，但不应该弹窗口或操作控件。

- `ProjectController`：持有当前打开的 `Project`，负责打开、保存、新建工程、未保存修改状态和自动备份写入。
- `FileRevision`：流式内容摘要；`ProjectController` 在加载、保存、覆盖确认和改名提交之间维护磁盘版本。
- `ProjectFileTransaction`：工程级写入锁、最终磁盘版本复核、原子提交与上一磁盘版本保护。
  改名及恢复工作流须在文件移动、工程写入和失败回滚全程持锁；格式层仍只负责序列化。
- `ProjectLocationService`：另存、备份、恢复以及旧撤销快照的来源路径重定位。窗口不能自行拼接相对引用。
- `ProjectRecoveryService`：版本化备份索引、保留策略、恢复副本与文件事务回放。`ProjectRecoveryController/Dialog` 只编排入口、确认和只读预览。
- `PageManagementDraft`：独立工程快照上的页面顺序与 pending 计划；移除页同步剪裁 pending。表格只派生候选效果，局部撤销不改真实工程。
- `ProjectTextSearchService`：范围与分组搜索、原文区间、工程指纹及批量替换规划。`ProjectSearchPanel` 不直接改标签，统一调用 `LabelEditController::replaceTexts`。
- `ReviewSessionService`：审阅状态、兼容旧批注、人工配对、变更指纹与版本化持久化。`ProofreadReviewPanel` 管局部状态 / 配对编辑与撤销，修改前使用与持久化相同的预算校验；不再提供批注编辑。人工对应约束交给 `ProjectComparisonService` 重新计算。`ReviewStatusDelegate` 只负责彩色图标和状态文字，不让 renderer 修改业务状态。
- `PreferenceStore`：设置文件基线、协作写锁、外改检测和原子提交，独立于工程恢复事务。窗口只管理草稿、显示错误和发出生效通知。
- `BoundedJsonIo`：恢复、重命名、审阅及偏好记录共用的有界 JSON 读写和结构校验。各服务负责语义、上限和事务，不在 UI 直接解析持久化记录。
- `FileRenameTransaction`：文件系统两阶段移动与两阶段回滚，保留无法恢复的文件及恢复映射；不操作工程模型或撤销栈。
- `ProjectWorkflowController`：承接合并工程、保存合并结果、页面顺序调整与对应撤销命令的项目级工作流。`MainWindow` 只负责弹出对话框和刷新 UI。
- `ProjectMergeService`：读取多个 LabelPlus 工程，按页生成合并计划，生成最终合并工程，并写入合并来源元数据。
- `ProjectPageOrderService`：校验并应用页面重排，负责在重排后保持来源元数据仍绑定到图片名。
- `ProjectImageRenameService`：按当前页序生成图片条目更新计划，校验 `{index}` / `{index:03}` / `{stem}` 格式串。对于已有图片，它执行文件系统两阶段重命名；对于缺失图片，它复用同一套计划把页面引用改为匹配到的已有图片，并采用该图片的实际扩展名。成功后负责迁移合并来源和校对基线元数据。它不注册普通撤销命令，因为该操作可能真实改动磁盘文件。
- `ProjectImageValidator`：扫描工程中缺失的图片文件，返回数据型诊断，由 UI 非阻塞展示。
- `PageSourceInfoService`：解析和重写统一项目元数据里的合并来源 section。UI 不能自己解析 comment 行。
- `ProjectComparisonService`：把校对基线或另一个工程与当前工程比较为结构化差异。UI 只展示结果，不直接实现
  页面匹配、label 匹配或 diff 规则。外部工程对比先生成 `ProjectComparisonPlan`，优先按规范化文件名匹配页面，再尝试轻量图片指纹匹配，必要时可按页序匹配。图片指纹读取和缓存也属于这个服务，不应散落到 UI。
- `LabelSequenceDiffService`：负责单页内的新旧 label 序列对齐。它不读取工程文件，也不生成 UI 文案。
- `TextDiffService` / `TextDiffHtmlRenderer`：前者使用 `diff-match-patch` 生成单个 label 文本差异，后者把差异渲染成 HTML。
- `ProofreadReportService`：生成校对 HTML 报告并保存到文件。文件选择和成功/失败提示仍属于 UI。
- `SessionStateStore`：通过 `QSettings` 保存本机布局、最近工程、每个工程的页码/缩放/视图中心/选中标签等会话状态。
- `LabelEditController`：新增、删除、移动、改文本、改类别、批量切换、重排标签和分组编辑，并注册撤销/重做。
- `LabelNavigator`：在当前分组筛选下查找上一条/下一条可见标签，跨页导航逻辑放这里而不是写在 UI 事件处理里。
- `ImagePageCache`：异步加载图片、维护 LRU 缓存、预加载相邻页。UI 只请求“显示当前页”，不直接管理加载线程和缓存命中。
  GUI 线程拥有 `QFutureWatcher`；工作线程只捕获 loader 和路径等值，不接触 QObject。
  加载前后、结果交付和缓存命中均验证文件内容摘要；变化时最多重试一次。清空缓存递增代次，旧任务不能回填新请求。
- `AutomationService` / `AutomationRunner`：脚本发现与密钥，以及进程、日志、取消和临时文件生命周期；不接收活动 Project。
- `AutomationManifestParser`：解析参数、密钥和非敏感环境声明；任务协议不混入表单解析。
- `AutomationPythonResolver`：根据偏好设置、内置候选和系统命令解析 Python 解释器，生成“Python 不可用”的用户提示。
- `ProjectRevisionTracker`：运行会话、单调字段版本与删除墓碑，随原生编辑和撤销发布检查点。
- `ProjectTaskService`：作用域快照、宿主依赖与权限、取消和单次提交；工作线程不能直接访问活动工程。
- `AutomationProtocol`：v2 任务范围规划、快照编码和严格输出解码，直接使用稳定 ID；不保留 v1 适配。
- 自动化与全文替换共用 `LabelEditController::commitChanges()` 注册批量撤销，详见
  [自动化基础契约](automation-foundation.md)。
- `SecretStore`：QtKeychain 封装。自动化脚本的 API key 等敏感信息只能通过这里进入系统密钥环。

## `src/ui`

`src/ui` 是 Qt Widgets 层。UI 类可以有交互状态，但应尽量把可复用规则拆到小控制器。

主要类：

- `MainWindow`：菜单、主布局、信号连接、状态栏提示和控制器编排。它可以调用服务，但不应该新增文件格式、脚本解析、标签编辑规则或图片加载状态机。
- `ImageCanvas`：左侧图像预览、标签标记绘制、悬停气泡、选区模式、缩放/拖动视图，以及点击/拖动标签标记时发出的用户意图信号。它不直接修改 `Project`。悬停气泡使用鼠标透明的顶层 tooltip 窗口显示，允许跨过画布边界但不能拦截右侧标签表格或滚动条的鼠标事件。手动缩放后窗口尺寸变化时，应以当前视口中心对应的 scene 点作为锚点，resize 后把它恢复到新的视口中心，避免布局变化造成视觉焦点跳动。
- `CanvasBottomBar`：左侧图像区域下边栏。它负责缩放滑条、标签/选区模式按钮、插入分组控件、翻页控件、页码下拉框、来源提示，以及窄宽度下的“更多工具”紧凑菜单。响应式布局、紧凑菜单同步和底栏最小宽度推导都应留在这里；`MainWindow` 只连接它暴露的控件和 `addGroupRequested()` / `removeGroupRequested()` 信号。
- `CanvasLabelItems`：标签标记与气泡等 `QGraphicsItem` 绘制细节。
- `CanvasLabelTextEditController` / `CanvasLabelTextEditor`：双击标签标记后出现的临时文本编辑框生命周期。
- `ImagePageViewController`：当前页图片显示协调。缓存命中、异步加载 request id、过期结果丢弃、相邻页预加载和延迟恢复视图都属于它。
- `ProjectViewController`：页码下拉框、来源显示、翻页控件刷新。
- `LabelSelectionController`：右侧表格、左侧标签标记和当前标签之间的选择同步。`Ctrl` 多选、`Shift` 连续选择，以及“用户点表格”“程序同步选择”“修订模式下悬停标记”等选择来源语义应集中在这里。新增选择路径时，应通过 `selectSingle()`、`selectIndexes()` 或 `clearSelection()` 传入正确来源，不要在 `MainWindow` 里手写表格/画布双向同步状态。
- `EditorStateController`：检测、提交和恢复当前活动文本编辑器。快捷键切换标签时保持编辑状态的逻辑放这里。
- `MainWindowShortcutController`：显式注册标签工作区后处理标签 / 翻页快捷键，只匹配按键并调用回调，不直接改工程数据；查找与其他表单保持原生键盘行为。翻页不再另设窗口级 QAction 重复截获。
- `AutomationController`：自动化菜单构建、脚本发现、脚本运行、日志窗口、取消运行和结果派发。不要再把自动化菜单和进程生命周期散落回 `MainWindow`。
- `ProofreadingController`：校对轮次与批注的交互编排、外部工程选择与异步比较、结果对话框启动和状态更新。`MainWindow` 只提供当前工程、偏好、选择跳转等回调，持久化提交委托 `ReviewRoundEditService`。
- `ProofreadChangeTableModel` / `ProofreadChangeFilterModel`：校对变更表唯一的数据与筛选/排序入口。对话框不得另存一份“过滤后行号”或用 `QTableWidgetItem` 复制业务状态。

跨目录工程比较使用图片指纹补充页面匹配。指纹缓存是进程内、线程安全且有容量上限的 LRU 缓存；
新增类似缓存时必须同时定义失效 key 和容量策略，不能使用无界静态 `QHash`。
- `AutomationShortcutController`：自动化脚本快捷键匹配和失效脚本处理。
- `AutomationRunDialog` / `AutomationParameterDialog`：自动化日志与运行前参数输入。日志呈现独立限制为 262,144 个 UTF-16 单元、5,000 个块，每行至多 4,096 个单元；只在用户位于末尾时跟随滚动。
- `LabelTableModel` / `CurrentPageLabelContext`：右侧表格通过只读 provider 获取当前页标签，维护可见行映射，
  发出编辑/重排请求，但不直接修改标签。不缓存 `ImageEntry` 内部容器地址；工程快照的写时复制不会留下悬空绑定。
  provider 的宿主必须比模型活得久，宿主析构和工程切换时清空绑定。数据结构变化仍须按模型协议刷新可见行。
- `LabelEditDelegates`：表格文本列和类别列的原地编辑控件。
- `PageOrderDialog` / `PageOrderListModel`：页面管理界面与对应模型，负责页面排序、移除页面，以及展示按当前顺序生成的图片文件重命名或缺失页引用修复预览；真正的文件系统重命名和项目引用更新仍交给 `ProjectImageRenameService`。
- `ProjectMergeDialog`：合并工程来源选择及当前冲突导航。仅使用两个 `MergeCandidatePreview`，后者复用 `ImagePageViewController` 和有界图片缓存，候选切换合并为最新请求，每个预览最多一个解码任务；不为隐藏冲突创建画布或解码图片。保留每页的来源选择和视口状态。

合并预览通过 `ImagePageCache::previewLoader` 选择受限解码策略，输出不超过 1600×1600，源图最多 33,554,432 像素；
源尺寸未知或超限只拒绝预览，不删除候选或禁止选择来源。主界面仍使用默认全分辨率加载，两种策略共用解码和文件版本验证，UI 不自行读取 / 解码图片。
- `GroupFilterComboBox`：右上角分组多选筛选。
- `PreferenceDialog`：偏好设置窗口，负责左侧分类导航、右侧页面栈和偏好 JSON 读写协调。复杂子页的表格逻辑应放到 `GroupStyleEditorWidget`、`AutomationShortcutEditorWidget` 或 `PreferenceDialogWidgets`。
- `ThemeManager`：加载和应用内置 Breeze 样式表。
- `ViewportFittedTableColumns`：让表格列宽跟随 viewport，并避免水平滚动条和末列拖动抖动。
- `DialogWindowUtils`：跨平台统一处理可最大化对话框的窗口标志。

## `MainWindow` 的职责上限

`MainWindow` 可以做：

- 创建 UI 控件和菜单。
- 连接控件、控制器和服务。
- 收集 UI 上下文并调用服务。
- 根据服务或控制器返回的结果刷新控件。
- 显示状态栏提示、消息框和文件选择对话框。

`MainWindow` 不应该做：

- 手写 LabelPlus 文本格式解析/序列化。
- 手写自动化 `script.json`、输入 JSON 或输出 JSON 解析。
- 持有图片异步加载 request 状态。
- 直接实现标签编辑规则和撤销命令。
- 直接操作 `QSettings` key。
- 解析合并来源注释块。
- 新增第二套快捷键解析。
- 在多个函数里重复维护表格/画布选择同步。

如果新功能需要新增一批 `m_` 成员变量，先判断它是否应该成为一个控制器或服务类。

## UI 刷新规则

不要把“重新加载图片”当成通用刷新手段。`ImageCanvas::setImage()` 会重建场景，可能改变视图位置和瞬时选择状态，只应在真实换页、打开工程或恢复会话时使用。

当前页标签改动时：

- 调用标签、表格和画布的局部刷新辅助函数。
- 不要因为文本、类别、标签标记样式或分组筛选变化而重载图片。
- 撤销/重做如果仍在当前页，应保持缩放和视图中心。

由于图片通过 `ImagePageCache` 异步加载，恢复缩放和视图中心必须等目标 pixmap 安装进 `ImageCanvas` 后再做。需要恢复视图时，使用 `ImagePageViewController::restoreViewAfterCurrentImageDisplayed()`。

`ImageCanvas::setImageLoading()` 表示真实换页，会清理当前页瞬时状态。`ImageCanvas::setImage()` 完成同一页加载后应保留已经恢复的选中标签标记，避免异步完成回调清掉会话恢复结果。

## 标签选择与视图聚焦

标签选择存在多个来源：用户在右侧表格点击、用户在左侧标记点击或拖动、修订模式下鼠标悬停标记、撤销/重做、删除和快捷键导航等。只有真正的右侧表格用户选择，才应该触发“修订模式下右侧表格选择后左侧画面聚焦到对应标记”的行为。其他来源只是同步选中状态，不应顺手改变图像视图中心或缩放。

因此选择来源统一由 `LabelSelectionController::SelectionOrigin` 表示。`MainWindow` 可以根据控制器报告的来源决定是否执行视图聚焦，但不应该自己维护额外的“正在程序选择”布尔状态。以后新增会改变选中标签的功能时，先判断它属于表格用户选择、程序同步选择还是画布悬停选择，再通过 `LabelSelectionController` 入口完成同步。

## 撤销/重做

所有可逆项目编辑都必须通过 `UndoStack`。当前已覆盖：

- 新增、删除、移动标签。
- 修改标签文本、类别和坐标。
- 批量删除、批量切换类别。
- 标签排序。
- 新增/删除分组。
- 页面顺序调整。
- 自动化脚本产生的新增、改文本、改类别、改坐标、删除标签操作。

按页序重命名图片文件、或为缺失图片页指定已有图片来源，都属于项目文件和文件系统边界上的批处理操作，不进入普通撤销栈。UI 必须在执行前提示用户该操作可能修改磁盘文件并在成功后清空当前撤销历史；实际重命名、引用更新和元数据迁移应通过 `ProjectImageRenameService` 完成，不要在 UI 中直接调用 `QFile::rename()` 或手写 `ImageEntry` 路径修复逻辑。

新增编辑操作时，要求：

- 同一变更内实现撤销和重做。
- 未保存修改状态和 UI 刷新与普通编辑保持一致。
- 标签和分组编辑优先走 `LabelEditController`。
- 自动化脚本只输出 `operations`，由 C++ 校验后应用，不允许脚本直接改 `.txt` 工程文件。

## 自动化脚本边界

自动化采用外部 Python 进程及 v2 SDK，不嵌入解释器，不兼容旧输出文件 API。
协议、字段范围和运行时并发规则以 [自动化基础契约](automation-foundation.md) 为准，避免在此重复维护 API 示例。

脚本只读取受范围约束的快照，结果由 `ProjectTaskService` 校验依赖并交给统一编辑入口提交。
禁止工作线程直接修改活动工程；密钥走 QtKeychain，不写进快照、输出或日志。
GUI 运行期间保持现有编辑限制；服务层按实际字段依赖拒绝过期结果，不把读取权限等同于写权限。
校对轮次/批注更新发布 Metadata 版本，仅依赖此资源的任务因此失效；无依赖的合法正文任务仍可提交。

菜单触发栈中不销毁或重建 action 树。运行状态只原位更新 enabled/visible，重新发现脚本延后执行。

## 项目元数据

LabelQt 会把自身需要随工程流转的内部元数据写入 LabelPlus comment 区。所有这类数据必须通过
`ProjectMetadataService` 进入统一的 `LabelQtMetadata` 块，不再新增独立的 `LabelQt...` comment 块。

```text
# LabelQtMetadata v1
# compression=qCompress
# encoding=base64
# payload=...
# EndLabelQtMetadata
```

压缩前的 JSON object 当前包含这些 section：

- `mergeSources`：合并工程后每段页面来源，由 `PageSourceInfoService` 读写。
- `review`：旧版校对基线，由 `ReviewMetadataService` 读取并可迁入新轮次。
- `proofreading`：当前轮次的原译稿、完成稿、独立批注和审阅状态，由 `ReviewRoundService` 管理。
- `identities`：工程、页面、Label、分组 UUID 及页面 Label 身份代次，由 `ProjectIdentityService` 管理。
- `labelText`：经典文本格式中结尾空行、CR/LF 等歧义的原文保真信息，由 `LabelPlusDocument` 按正文校验值恢复。

这个块只是 `qCompress` 压缩和 base64 编码，不是加密。不要在其中保存 API key、访问令牌或其他秘密。
新增工程内 metadata 时，应给统一 JSON object 增加新 section，并让对应服务只读写自己的 section。这样可以
最大化压缩效率，也能避免 comment 区散落多个相互重写的私有块。

## 校对元数据

当前校对采用一轮原译稿/完成稿快照和独立批注，不记录每次按键，不引入逐条接受/拒绝修订。
`ReviewRoundService` 管理 `proofreading` v1、旧基线读取及展示结果组合；
`ReviewRoundEditService` 负责主线程、版本检查和只更新所属 section 的普通撤销。
快照不包含嵌套轮次，批注按对象 UUID 关联，审阅状态按差异版本关联。

`ProjectComparisonService` 在同一身份谱系内优先匹配页面 UUID；Label 身份代次相同则由
`LabelSequenceDiffService::diffByIdentity` 精确配对。外部改写使身份失效时保留原启发式回退并提示，
不能把旧批注按行号自动挂回。单个 Label 的文本差异继续由 `TextDiffService` 统一产生。

`ProofreadingController` 只编排菜单、确认与输入，不承担正文编辑或文件写入；
`ProofreadNotesWidget` 不缓存活动工程，只缓存批注展示行。`ProofreadChangesDialog` 与
`ProofreadReportService` 共用差异及批注结果。完成轮次固定文本结果，页面改名后预览可按身份读取当前图片；
另存/恢复的快照路径重定位由 `ProjectLocationService` 负责，不改写历史页名。

`ProjectController::documentSession` 隔离不同次打开的文档，允许同一文档另存后继续普通撤销；
自动化的任务会话仍在另存后失效，二者生命周期不可混用。
详细操作、兼容边界及交互测试见 [校对轮次](proofreading-rounds.md)，算法见
[校对差异](proofreading-diff.md)。

## 偏好设置

运行时 UI 配置通过 `AppPreferences` 和 `preference.json` 管理。不要在 UI 类里直接解析 JSON。

当前主要偏好：

- `appearance.style`：Qt 控件风格。空值表示系统默认，候选值来自 `QStyleFactory::keys()`。
- `appearance.theme`：内置 Breeze QSS 主题。空值表示不使用应用样式表，目前暴露 `breezeDark` 和 `breezeLight`。
- `appearance.language`：界面语言。空值表示跟随系统；语言变更需要重启生效。
- `automation.showRunLog`：是否显示自动化 stdout/stderr 日志窗口。
- `automation.python.command` / `arguments` / `autoInstallRequirements` / `pipIndexUrl`：自动化 Python 解释器和依赖安装设置。
- `automation.shortcuts`：用户给自动化脚本绑定的快捷键。
- `labelMarker`、`groupStyles`：标签标记默认样式和按分组覆盖的样式。
- `labelTable`、`labelTextEditor`、`markerTextBubble`、`canvasLabelTextEditor`：表格、文本框、气泡和临时编辑框的字体、透明度和显示细节。`markerTextBubble.maxLineCharacters` 控制气泡单行软换行阈值；真实换行应保留可见换行标记。
- `input`：主窗口快捷键和修饰键。
- `backupPath`、`backupIntervalSeconds`：自动备份配置。
- `backupMaximumCount`：每工程自动备份保留上限，0 为全部保留；只清理经核实的自动记录。
- `input.findProjectShortcut`、`nextSearchMatchShortcut`、`previousSearchMatchShortcut`：查找导航键位；冲突检查统一由 AppPreferences 提供，避免 UI 和运行时各自定义优先级。

偏好设置窗口使用“应用 / 关闭”：先通过 `PreferenceStore` 保存成功再应用；关闭未应用草稿时确认，失败不关闭。每个主窗口只保留一个偏好窗口，重复打开激活原窗口。高级菜单提供重新载入、JSON 预览和外部编辑；外改冲突拒绝覆盖。左侧搜索匹配分类和设置项文字，右侧仍使用职责分明的子页，不把不同配置堆回单个页面。

新增偏好项时通常要同步：

- `AppPreferences.h/.cpp`
- `preference.json`
- `PreferenceDialog` 或对应子控件
- `README.md`
- 本文档或 `docs/code-walkthrough.md`
- `tests/LabelTests.cpp`
- 如果有新 UI 文本，更新四份 `translations/labelqt_*.ts`

## 会话状态

本机状态使用 `QSettings`，通过 `SessionStateStore` 读写，不写入 `preference.json`。

当前会话状态包括：

- 主窗口几何和 splitter。
- 最近打开工程列表。
- 每个工程上次停留的页。
- 每个工程的缩放、归一化视图中心。
- 当前标签和多选标签。
- 表格列宽、合并后是否自动打开、不同用途文件对话框的上次目录等本机偏好。

文件/目录选择对话框的路径记忆必须按用途区分，例如打开工程、保存工程、合并工程、导出校对报告不能共享同一个 `lastDirectory`，避免一次导出操作改变其他工作流的默认目录。

恢复时必须做边界检查。工程文件被外部改动、页面减少、图片缺失或标签下标失效时，应安全退回到仍然有效的页/选择。

## 合并来源元数据

合并工程会在统一项目元数据的 `mergeSources` section 里写入页面来源信息。

规则：

- 每个 JSON object 描述最终工程图片顺序中的一个连续区间。
- `sourceIndex` 是用户选择合并文件时的一基序号。
- `sourcePath` 相对于合并后工程的保存目录。
- `pageCount` 是区间页数。
- `labelCount` 是该区间非删除标签总数。
- 页面重排后必须按新顺序重写这块信息。

解析和重写都由 `PageSourceInfoService` / `ProjectPageOrderService` 负责。UI 只消费结构化结果，例如在左侧图像下边栏显示当前页来源。

## 外部工程文件变化

- `ProjectFileMonitor`：文件/目录监听、防抖、后台摘要和解析校验、轮询补查，以及过期结果丢弃。
- `ProjectController::storageChanged`：成功持久化或加载后的通知。只有工程控制器能推进加载/保存基线。
- `LabelPlusDocument::readFromFile`：返回同一次字节读取对应的解析工程和摘要，供加载、监听和备份校验复用。
- `ProjectExternalChangeController`：状态栏入口与提示时机，不读取工程文本、不提交修改。
- `ProjectRecoveryController`：复用主动提示和保存冲突的交互；重载前备份交给 `ProjectRecoveryService`。
- `SessionStateStore`：保存本机页面/Label ID，恢复时优先使用身份，而非外部重排前的行号。

监听不得直接从文件事件槽重载工程；主窗口只连接编辑提交、受版本约束的加载和视图恢复。
自动化运行和模态工作流中延迟询问，具体失败语义见 [持久化安全](persistence-safety.md)。

## 国际化

项目使用 Qt Linguist：

- 所有用户可见 UI 文本使用 `tr()`。
- 更新 UI 文本时同步四份翻译文件：
  - `translations/labelqt_zh_CN.ts`
  - `translations/labelqt_zh_TW.ts`
  - `translations/labelqt_ja_JP.ts`
  - `translations/labelqt_en_US.ts`
- 运行 `python scripts/check_translations.py`。
- 构建时通过 `qt_add_translations()` 将 `.qm` 编进资源路径 `/i18n`。

`TranslationManager` 会从 `:/i18n` 和可执行文件旁的 `i18n` 目录发现语言。当前发行方式主要依赖内置资源，因此二进制旁边没有 `i18n` 目录时仍可切换已编入的语言。

## 测试策略

持久化、重命名事务、正文保真和自动化窗口生命周期的失败语义见 [持久化安全说明](persistence-safety.md)。
重命名后的工程保存必须属于事务提交步骤，成功前不发布候选工程，也不清空撤销历史。

GUI 项目不应只依赖人工点击。当前测试包括 `LabelQtTests`、`ProofreadChangeTableModelTests`、
`PersistenceSafetyTests`、`ProductWorkflowTests`、`AutomationTaskTests`、`AutomationProtocolTests`、
`RiskRegressionTests` 和 `ExternalProjectChangeTests`，覆盖主要功能、校对表格合约、
持久化失败路径、生命周期、外部文件变化、跨模块工作流及自动化身份/并发提交。主要覆盖：

- LabelPlus 读写 round-trip。
- `AppPreferences` 默认值、错误回退和序列化。
- `LabelNavigator` 跨页导航和分组筛选。
- `AutomationTaskTests` 的稳定身份、字段冲突、线程归属、取消与原子批量提交。
- `AutomationProtocolTests` 的 v2 严格协议、范围和真实 Python SDK 到共用编辑撤销的端到端测试。
- `LabelTableModel`、`PageOrderListModel` 的 `QAbstractItemModelTester` 合约测试。
- `SessionStateStore` 多选恢复。
- 缺失图片扫描。
- `ImageCanvas` 中 Ctrl 点击与 Ctrl 拖动标签标记的交互测试。
- 合并工程、页面来源注释、页面顺序调整。

新增模型/视图行为时，优先加 `QAbstractItemModelTester`。新增容易回归的鼠标/键盘交互时，优先加轻量 `QTest`，并保持测试可在 offscreen 平台运行。

## 依赖与许可边界

主程序当前硬依赖：

- Qt 6 `Core`、`Gui`、`Widgets`、`Concurrent`
- QtKeychain

增强或可选依赖：

- Qt Svg：可用时链接，让内置 Breeze SVG 图标显示更完整。
- Qt LinguistTools：构建翻译资源时使用。
- Python 3：运行自动化脚本时需要，主程序本体不嵌入 Python。

项目应保持在 LGPL 兼容的 Qt 模块范围内，除非明确决定改变发布策略。不要引入 Qt GPL-only 模块，除非已经记录许可影响并获得项目决策。

发布源码仓库时不要提交 Qt SDK、Qt 源码或 Qt 运行时二进制文件。发布二进制包时，需要提供 Qt、QtKeychain、BreezeStyleSheets 以及随包脚本/资源的第三方声明。正式二进制发布矩阵为 Windows 动态版、通过专项许可与重链接审查的 Windows 静态版，以及 Linux RPM。Linux 静态构建和 DEB 包仅为实验性方向，不作为受支持的正式发行产物。

仓库内置 BreezeStyleSheets 资源位于 `resources/themes/breeze`，保留了上游 MIT 与 Apache 2.0 声明。更新这些资源时必须同步 `THIRD_PARTY_NOTICES.md` 和本地许可证文件。
