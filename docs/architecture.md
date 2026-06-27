# 架构说明

LabelQt 是一个独立的 C++/Qt 6 Widgets 应用。它的核心目标是编辑经典 LabelPlus `.txt` 工程，同时支持合并、页序调整、自动化脚本、OCR 与 AI 翻译等扩展工作流。

这份文档记录当前代码的职责边界。新增功能前请先确认它应该放在哪一层，避免把新的状态机、文件格式处理或业务规则继续塞回 `MainWindow`。

## 总体分层

```text
src/core       平台无关的数据模型、文件格式、偏好设置、撤销栈
src/services   工程工作流、会话状态、自动化、图片缓存、密钥、压缩包等应用服务
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

## `src/core`

`src/core` 是工程数据和基础规则层。

- `Label`：单个标签，包含文本、分组、归一化坐标和软删除状态。
- `Project` / `ImageEntry`：当前工程的内存模型。`Project` 持有页面列表、分组、工程路径、注释区等信息；`ImageEntry` 持有图片名、图片路径和该页标签。
- `LabelPlusDocument`：经典 LabelPlus `.txt` 工程格式的解析和保存。格式兼容逻辑应集中在这里，不要在 UI 中拼接工程文本。
- `AppPreferences`：`preference.json` 的类型化读取、默认值、序列化和诊断警告。偏好缺失、JSON 损坏或类型错误时应回退到内置默认值并返回警告。
- `ApplicationTheme`：内置 Breeze 样式表主题注册。
- `TranslationManager`：发现和加载 `labelqt_*.qm` 翻译资源。
- `UndoStack`：对 Qt `QUndoStack` / `QUndoCommand` 的薄包装，项目内可逆编辑都应通过它进入撤销/重做系统。

新增项目字段时，通常需要同时检查：

- `Label` / `Project`
- `LabelPlusDocument`
- `AutomationService` 导出的 JSON 快照
- 相关 UI 模型和委托
- `tests/LabelTests.cpp`

## `src/services`

`src/services` 放应用服务。它们可以被 UI 调用，但不应该弹窗口或操作控件。

- `ProjectController`：持有当前打开的 `Project`，负责打开、保存、新建工程、未保存修改状态和自动备份写入。
- `ProjectWorkflowController`：承接合并工程、保存合并结果、页面顺序调整与对应撤销命令的项目级工作流。`MainWindow` 只负责弹出对话框和刷新 UI。
- `ProjectMergeService`：读取多个 LabelPlus 工程，按页生成合并计划，生成最终合并工程，并写入合并来源元数据。
- `ProjectPageOrderService`：校验并应用页面重排，负责在重排后保持来源元数据仍绑定到图片名。
- `ProjectImageValidator`：扫描工程中缺失的图片文件，返回数据型诊断，由 UI 非阻塞展示。
- `PageSourceInfoService`：解析和重写统一项目元数据里的合并来源 section。UI 不能自己解析 comment 行。
- `ProjectComparisonService`：把校对基线或另一个工程与当前工程比较为结构化差异。UI 只展示结果，不直接实现
  label 匹配或 diff 规则。
- `SessionStateStore`：通过 `QSettings` 保存本机布局、最近工程、每个工程的页码/缩放/视图中心/选中标签等会话状态。
- `LabelEditController`：新增、删除、移动、改文本、改类别、批量切换、重排标签和分组编辑，并注册撤销/重做。
- `LabelNavigator`：在当前分组筛选下查找上一条/下一条可见标签，跨页导航逻辑放这里而不是写在 UI 事件处理里。
- `ImagePageCache`：异步加载图片、维护 LRU 缓存、预加载相邻页。UI 只请求“显示当前页”，不直接管理加载线程和缓存命中。
- `ArchiveReader`：压缩包读取封装。检测到 libarchive 时支持更多格式。
- `AutomationService`：发现自动化脚本、生成输入 JSON、运行外部 Python 进程、读取输出 JSON。
- `AutomationManifestParser`：解析 `script.json`、参数声明、密钥声明和脚本输出。脚本清单的字段变更应集中在这里。
- `AutomationPythonResolver`：根据偏好设置、内置候选和系统命令解析 Python 解释器，生成“Python 不可用”的用户提示。
- `AutomationOperationApplier`：校验自动化脚本输出的 `operations`，把它们转换为项目变更计划，供 C++ 侧统一应用和撤销。
- `SecretStore`：QtKeychain 封装。自动化脚本的 API key 等敏感信息只能通过这里进入系统密钥环。

## `src/ui`

`src/ui` 是 Qt Widgets 层。UI 类可以有交互状态，但应尽量把可复用规则拆到小控制器。

主要类：

- `MainWindow`：菜单、主布局、信号连接、状态栏提示和控制器编排。它可以调用服务，但不应该新增文件格式、脚本解析、标签编辑规则或图片加载状态机。
- `ImageCanvas`：左侧图像预览、标签标记绘制、悬停气泡、选区模式、缩放/拖动视图，以及点击/拖动标签标记时发出的用户意图信号。它不直接修改 `Project`。
- `CanvasLabelItems`：标签标记与气泡等 `QGraphicsItem` 绘制细节。
- `CanvasLabelTextEditController` / `CanvasLabelTextEditor`：双击标签标记后出现的临时文本编辑框生命周期。
- `ImagePageViewController`：当前页图片显示协调。缓存命中、异步加载 request id、过期结果丢弃、相邻页预加载和延迟恢复视图都属于它。
- `ProjectViewController`：页码下拉框、来源显示、翻页控件刷新。
- `LabelSelectionController`：右侧表格、左侧标签标记和当前标签之间的选择同步。`Ctrl` 多选、`Shift` 连续选择等语义应集中在这里。
- `EditorStateController`：检测、提交和恢复当前活动文本编辑器。快捷键切换标签时保持编辑状态的逻辑放这里。
- `MainWindowShortcutController`：主窗口快捷键路由，只匹配按键并调用回调，不直接改工程数据。
- `AutomationController`：自动化菜单构建、脚本发现、脚本运行、日志窗口、取消运行和结果派发。不要再把自动化菜单和进程生命周期散落回 `MainWindow`。
- `AutomationShortcutController`：自动化脚本快捷键匹配和失效脚本处理。
- `AutomationRunDialog` / `AutomationParameterDialog`：自动化日志与运行前参数输入。
- `LabelTableModel`：右侧标签表格模型。它持有当前页标签指针，维护可见行映射，发出编辑/重排请求，但不直接修改标签。
- `LabelEditDelegates`：表格文本列和类别列的原地编辑控件。
- `PageOrderDialog` / `PageOrderListModel`：页面顺序调整界面与对应模型。
- `ProjectMergeDialog`：合并工程冲突解决窗口，复用只读 `ImageCanvas` 预览候选页。
- `GroupFilterComboBox`：右上角分组多选筛选。
- `PreferenceDialog`：偏好设置窗口。复杂子页的表格逻辑应放到 `GroupStyleEditorWidget`、`AutomationShortcutEditorWidget` 或 `PreferenceDialogWidgets`。
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

## 撤销/重做

所有可逆项目编辑都必须通过 `UndoStack`。当前已覆盖：

- 新增、删除、移动标签。
- 修改标签文本、类别和坐标。
- 批量删除、批量切换类别。
- 标签排序。
- 新增/删除分组。
- 页面顺序调整。
- 自动化脚本产生的新增、改文本、改类别、改坐标、删除标签操作。

新增编辑操作时，要求：

- 同一变更内实现撤销和重做。
- 未保存修改状态和 UI 刷新与普通编辑保持一致。
- 标签和分组编辑优先走 `LabelEditController`。
- 自动化脚本只输出 `operations`，由 C++ 校验后应用，不允许脚本直接改 `.txt` 工程文件。

## 自动化脚本边界

自动化脚本采用外部 Python 进程，不嵌入 Python 解释器。仓库里的脚本目录：

```text
scripts/official    官方脚本和示例
scripts/custom      用户自定义脚本
```

运行时脚本分成 official 和 custom 两个来源，各自最多选择一个目录，避免重复加载。official 优先从可执行文件目录旁边的 `scripts/official` 读取，否则从安装数据目录（例如 `/usr/share/labelqt/scripts/official`）读取；custom 优先从可执行文件目录旁边的 `scripts/custom` 读取，否则从用户脚本目录读取。仓库里的 `scripts/official` 主要作为官方脚本模板和示例，开发构建与 Windows 便携包可以直接携带它；用户自定义脚本建议放在用户脚本目录中。每个脚本目录包含 `script.json` 和 Python 入口文件。`script.json` 可以定义单个脚本，也可以用顶层 `scripts` 数组定义多个子脚本。菜单显示顺序必须与脚本清单顺序一致，方便配置脚本始终排在第一项。

脚本输入/输出规则：

- 主程序导出 `input.json`，包含工程快照、当前页、当前选中标签、当前图像选区和用户参数。
- 默认只导出未删除标签。
- `labelIndex` 是内部 label 下标，用于修改操作；`visibleIndex` 是 UI 展示序号，只能用于显示。
- 脚本写回 `output.json`，成功时可包含 `summary`、`result`、`quiet` 和 `operations`。
- `quiet: true` 只跳过成功结果弹窗，失败仍必须提示。
- 密钥参数存入系统密钥环，运行时由主程序注入环境变量。不要把 API key 写入脚本清单、配置、输入/输出 JSON 或日志。

运行自动化脚本时，编辑入口应禁用，避免脚本基于旧快照产生操作。但只读操作可以保留，例如翻页、缩放、拖动画面和悬停查看气泡。

不要在自动化 action 的触发栈内销毁或重建整个自动化菜单。脚本运行状态变化时只更新 action enabled 状态；需要重新发现脚本时，使用显式刷新或在当前调用栈结束后延迟执行。

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
- `review`：校对基线快照，由 `ReviewMetadataService` 读写。

这个块只是 `qCompress` 压缩和 base64 编码，不是加密。不要在其中保存 API key、访问令牌或其他秘密。
新增工程内 metadata 时，应给统一 JSON object 增加新 section，并让对应服务只读写自己的 section。这样可以
最大化压缩效率，也能避免 comment 区散落多个相互重写的私有块。

## 校对元数据

校对流程应贴合校对人员直接编辑工程的习惯：校对人员打开工程后仍在主界面修改文本、增删
label、移动 marker 和调整顺序。校对功能只提供一层透明记录和汇总，不引入 Word 式接受/拒绝修订流程。

`ReviewMetadataService` 维护 `review` section，用于保存“开始校对”时的 label 基线快照。之后的校对变更由
`ProjectComparisonService` 按页组织，并交给 `LabelSequenceDiffService` 把每页 label 列表看作类似 diff
里的行序列进行启发式对齐。匹配主要关注文本，页内顺序用于保序对齐，类别和坐标只作为弱消歧信号。这样可以在
不向经典 LabelPlus 文本主体引入稳定 ID 的前提下，识别常见的文本修改、增删、marker 变化和页内顺序移动。
如果工程经过外部工具编辑导致文本变化过大或重复短句过多，校对系统可能退化为新增/删除或粗粒度修改；这种情况
下功能可以降级，但不能崩溃。

详细算法、判定逻辑和已知边界见 [校对差异算法说明](proofreading-diff.md)。

开始或替换校对基线本质上只修改 `commentLines`，必须走 `UndoStack`。变更汇总窗口只读展示差异并提供跳转，
不承担 label 编辑职责；具体编辑仍应继续通过 `LabelEditController` 等现有路径完成。

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
- `labelTable`、`labelTextEditor`、`markerTextBubble`、`canvasLabelTextEditor`：表格、文本框、气泡和临时编辑框的字体/透明度。
- `input`：主窗口快捷键和修饰键。
- `backupPath`、`backupIntervalSeconds`：自动备份配置。

偏好设置窗口现在只有“保存”语义：保存到 `preference.json` 后立即应用。`Reload` 用于从磁盘重新读取。打开窗口时显示的是当前运行时 `AppPreferences`，而不是另写一套 UI 默认值。

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
- 表格列宽、合并后是否自动打开等本机偏好。

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

GUI 项目不应只依赖人工点击。当前测试集中在 `tests/LabelTests.cpp`，覆盖：

- LabelPlus 读写 round-trip。
- `AppPreferences` 默认值、错误回退和序列化。
- `LabelNavigator` 跨页导航和分组筛选。
- `AutomationOperationApplier` 的操作计划和撤销/重做应用。
- `LabelTableModel`、`PageOrderListModel` 的 `QAbstractItemModelTester` 合约测试。
- `SessionStateStore` 多选恢复。
- 缺失图片扫描。
- `ImageCanvas` 中 Ctrl 点击与 Ctrl 拖动标签标记的交互测试。
- 合并工程、页面来源注释、页面顺序调整。

新增模型/视图行为时，优先加 `QAbstractItemModelTester`。新增容易回归的鼠标/键盘交互时，优先加轻量 `QTest`，并保持测试可在 offscreen 平台运行。

## 依赖与许可边界

主程序当前硬依赖：

- Qt 6 `Core`、`Gui`、`Widgets`
- QtKeychain

增强或可选依赖：

- Qt Svg：可用时链接，让内置 Breeze SVG 图标显示更完整。
- Qt LinguistTools：构建翻译资源时使用。
- LibArchive：可用时启用更多压缩包读取格式。
- Python 3：运行自动化脚本时需要，主程序本体不嵌入 Python。

项目应保持在 LGPL 兼容的 Qt 模块范围内，除非明确决定改变发布策略。不要引入 Qt GPL-only 模块，除非已经记录许可影响并获得项目决策。

发布源码仓库时不要提交 Qt SDK、Qt 源码或 Qt 运行时二进制文件。发布二进制包时，需要提供 Qt、QtKeychain、BreezeStyleSheets、LibArchive 以及随包脚本/资源的第三方声明。Windows `LabelQtStatic` 目标只是本地实验目标，不应在未做 Qt 许可证审查前作为官方发行产物。

仓库内置 BreezeStyleSheets 资源位于 `resources/themes/breeze`，保留了上游 MIT 与 Apache 2.0 声明。更新这些资源时必须同步 `THIRD_PARTY_NOTICES.md` 和本地许可证文件。
