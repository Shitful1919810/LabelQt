# C++ 代码导览

这份文档面向第一次阅读 LabelQt C++/Qt 代码的人。它不替代 `docs/architecture.md`，而是回答“从哪里开始看”“一个功能经过哪些类”“修改时落点在哪里”。

## 入口与构建目标

程序入口是 `src/main.cpp`，启动流程很短：

1. 创建 `QApplication`。
2. 读取 `AppPreferences`。
3. 应用 Qt 控件风格和内置 Breeze QSS 主题。
4. 设置应用名、组织名和版本。
5. 通过 `TranslationManager` 安装界面翻译。
6. 用 `QCommandLineParser` 解析可选工程路径。
7. 创建并显示 `MainWindow`。
8. 如果命令行给了工程文件，就打开该文件；否则尝试恢复最近一次工程。

可执行目标在 `src/CMakeLists.txt` 中定义。源码按三组加入目标：

- `LABELQT_CORE_SOURCES`：数据、格式、偏好、翻译、撤销。
- `LABELQT_SERVICE_SOURCES`：工程、自动化、图片缓存、会话、密钥、压缩包等服务。
- `LABELQT_UI_SOURCES`：Qt Widgets 界面和 UI 控制器。

CMake 会把根目录 `preference.json` 和 `scripts/official` 同步到构建出的可执行文件目录，方便开发构建和 Windows 便携包直接使用默认配置与官方脚本。运行时找不到 exe 旁边的官方脚本时，会使用安装到 `/usr/share/labelqt/scripts/official` 的只读脚本；用户自定义脚本则回退到用户数据目录。Linux 安装包不安装可编辑的 `preference.json`。翻译 `.qm` 通过 Qt 资源编进程序，不依赖运行目录下的 `i18n` 文件夹。

## 推荐阅读顺序

如果只想先熟悉主线，建议按这个顺序读：

1. `src/main.cpp`
2. `src/core/Project.h`、`src/core/Label.h`
3. `src/core/LabelPlusDocument.cpp`
4. `src/core/AppPreferences.h`
5. `src/ui/MainWindow.h`
6. `src/ui/MainWindow.cpp` 的构造函数、`createActions()`、`createCentralWidget()` 和项目打开/保存相关函数
7. `src/services/ProjectController.cpp`
8. `src/services/LabelEditController.cpp`
9. `src/ui/ImageCanvas.cpp`
10. `src/ui/ImagePageViewController.cpp`
11. `src/ui/LabelTableModel.cpp`
12. `src/ui/AutomationController.cpp`
13. `src/services/ProjectComparisonService.cpp`
14. `src/services/LabelSequenceDiffService.cpp`
15. `src/services/TextDiffService.cpp`
16. `src/ui/ProofreadChangesDialog.cpp`
17. `tests/LabelTests.cpp`

读 `MainWindow.cpp` 时不要从头到尾硬啃。更有效的方式是围绕一个功能追信号和槽，例如“点击标签标记多选”“新增标签”“运行自动化脚本”“调整页面顺序”。

## 核心数据模型

### `Label`

文件：`src/core/Label.h`、`src/core/Label.cpp`

`Label` 表示一个标签：

- `text`：标签文本。
- `group`：所属分组。
- `position`：相对于图片宽高的归一化坐标。
- `deleted`：软删除标记。

UI 上显示的序号是可见标签序号，也就是跳过已删除标签后的 `visibleIndex`。内部操作仍使用真实下标 `labelIndex`，这样删除后撤销/重做和自动化脚本操作可以稳定定位原对象。

### `Project` 与 `ImageEntry`

文件：`src/core/Project.h`、`src/core/Project.cpp`

`Project` 是当前工程上下文：

- `images()`：页面列表。
- `groups()`：分组列表。
- `commentLines()`：LabelPlus comment 区，包括统一的 LabelQt 项目元数据块。
- `filePath()`：当前 `.txt` 工程路径。
- `sourceName()`：工程来源名称，主要用于合并预览。

`ImageEntry` 表示一页：

- `name`：图片文件名。
- `path`：图片路径。
- `labels`：该页标签。

当前打开工程由 `ProjectController` 持有，UI 通过 `MainWindow::project()` 访问当前工程，但具体修改应尽量交给控制器或服务类。

### `LabelPlusDocument`

文件：`src/core/LabelPlusDocument.h`、`src/core/LabelPlusDocument.cpp`

负责 LabelPlus `.txt` 格式读写：

- `loadFromFile()`：解析文本工程，生成 `Project`。
- `saveToFile()`：将 `Project` 写回 LabelPlus 文本格式。

如果要兼容更多 LabelPlus 细节，优先改这里。不要在 UI 中直接拼接 `.txt` 文件内容。

### `AppPreferences`

文件：`src/core/AppPreferences.h`、`src/core/AppPreferences.cpp`

负责 `preference.json` 的默认值、读取、校验、警告和序列化。它覆盖：

- 外观主题和语言。
- 自动化 Python、日志和快捷键设置。
- 标签标记、分组样式、气泡和临时编辑框样式。
- 标签表格与文本编辑器字体。
- 主窗口快捷键与修饰键。
- 自动备份路径与间隔。

偏好缺失、损坏或类型错误时会回退到代码内置默认值，并把问题返回给 UI 显示在状态栏。

### `UndoStack`

文件：`src/core/UndoStack.h`、`src/core/UndoStack.cpp`

这是项目对 Qt `QUndoStack` / `QUndoCommand` 的包装。命令通常带有：

- 命令文本。
- 撤销消息。
- 重做消息。
- 撤销回调。
- 重做回调。

标签、分组、页面顺序和自动化操作都应通过它进入撤销/重做系统。不要新增一套局部撤销逻辑。

## 工程工作流

### `ProjectController`

文件：`src/services/ProjectController.h`、`src/services/ProjectController.cpp`

负责当前工程生命周期：

- 打开 `.txt` 工程。
- 保存、另存。
- 从图片目录新建工程。
- 维护未保存修改状态。
- 自动备份。

它不弹窗口。文件选择、确认保存、错误提示由 UI 层负责。

### `ProjectWorkflowController`

文件：`src/services/ProjectWorkflowController.h`、`src/services/ProjectWorkflowController.cpp`

承接较大的项目级工作流：

- 创建合并计划。
- 生成合并预览工程。
- 保存合并后工程。
- 应用页面顺序调整并注册撤销命令。

`MainWindow` 在这里的职责是弹出 `ProjectMergeDialog` / `PageOrderDialog`，然后把用户选择交给控制器。

### `ProjectMergeService`

文件：`src/services/ProjectMergeService.h`、`src/services/ProjectMergeService.cpp`

负责多工程按页合并：

- 读取多个 LabelPlus `.txt`。
- 按图片名收集候选页。
- 对无冲突页直接合并。
- 对冲突页生成候选，让 `ProjectMergeDialog` 决定采用哪一份。
- 保存前可按最终页序生成合并结果。
- 写入统一 `LabelQtMetadata` 里的合并来源 section。

合并来源元数据的解析和重写由 `PageSourceInfoService` 配合完成；压缩 comment 块本身由
`ProjectMetadataService` 统一处理。

### 校对与工程对比

主要文件：

- `src/services/ProjectComparisonService.cpp`
- `src/services/LabelSequenceDiffService.cpp`
- `src/services/TextDiffService.cpp`
- `src/services/TextDiffHtmlRenderer.cpp`
- `src/services/ProofreadReportService.cpp`
- `src/services/ReviewMetadataService.cpp`
- `src/ui/ProofreadChangesDialog.cpp`

校对功能有两种入口：

- 内部校对：在当前工程的统一 `LabelQtMetadata` 块里保存“开始校对”时的基线快照。
- 外部对比：临时把用户选择的另一个工程捕获为基线快照，不要求对方工程含有 metadata。

两种入口最终都走 `ProjectComparisonService` 和 `LabelSequenceDiffService`。外部工程对比会先生成 `ProjectComparisonPlan`，按规范化文件名匹配页面，再对剩余页面尝试轻量图片指纹匹配；如果自动匹配不足且页数相同，可以由用户选择按页序匹配。图片指纹只解码小缩略图，并按路径、大小和修改时间做内存缓存，避免大图工程在比较时重复完整读图。算法把每页 label
列表看成类似文本 diff 的行序列，先用保序精确文本对齐建立骨架，再对剩余项按文本相似度、类别、坐标和原序号进行启发式配对。单个 label 文本内部的高亮由 `TextDiffService` 和 `diff-match-patch` 完成，HTML 片段由
`TextDiffHtmlRenderer` 生成。

`ProofreadChangesDialog` 只负责展示差异、同步两侧只读预览、跳转到主界面 label，以及调用
`ProofreadReportService` 导出 HTML 报告。它不参与 label 匹配，也不修改工程。报告按页分组展示变更，并把压缩后的页面图像内嵌到 HTML。导出由 `ProofreadChangesDialog` 放到后台执行并显示进度提示，避免大图报告生成时阻塞界面。左右预览窗口分别从基线工程和当前工程读取图片路径，页面匹配只影响 label 对比，不把两侧图片路径混成一个来源。

外部工程对比可能需要读取多张大图生成指纹。主界面入口会显示“正在比较工程”的模态进度提示，并通过 Qt Concurrent 在后台生成 `ProjectComparisonPlan` 和差异列表；计算期间主界面不会假死，也不会允许用户同时修改当前工程造成结果错位。

### `ProjectImageValidator`

文件：`src/services/ProjectImageValidator.h`、`src/services/ProjectImageValidator.cpp`

打开工程后扫描图片路径是否存在。它只返回缺失项，UI 决定如何非阻塞提示用户。

## 标签编辑主线

标签编辑原则：UI 发出意图，`LabelEditController` 修改数据和注册撤销命令，UI 再刷新视图。

### 新增标签

1. 用户在 `ImageCanvas` 的标签模式下点击图像。
2. `ImageCanvas` 发出 `labelCreateRequested(QPointF)`。
3. `MainWindow::addLabel()` 检查当前分组是否可见和工程是否可编辑。
4. `LabelEditController::addLabel()` 新增标签并注册撤销/重做。
5. `MainWindow` 刷新表格、画布和当前选择。

### 移动标签标记

1. 用户按住偏好设置中的 `input.moveLabelModifier` 拖动标签标记。
2. `ImageCanvas` 发出 `labelMoveRequested(index, position)`。
3. `MainWindow::moveLabel()` 调用 `LabelEditController`。
4. 当前页只刷新标签标记和表格状态，不重载图片。

### 表格原地编辑

1. `LabelTableModel::setData()` 不直接改 `Label`。
2. 它发出 `labelEditRequested(sourceIndex, column, newValue)`。
3. `MainWindow::updateLabelFromTable()` 根据列类型调用 `LabelEditController`。
4. 撤销/重做、未保存修改状态和画布刷新统一走控制器路径。

### 删除、多选和批量切换类别

右侧表格和左侧标签标记的多选状态由 `LabelSelectionController` 同步。删除键、右键菜单、标签标记点击多选最终都应汇总成当前页的源下标，再交给 `LabelEditController`。

## 图像显示主线

### `ImageCanvas`

文件：`src/ui/ImageCanvas.h`、`src/ui/ImageCanvas.cpp`

负责可视交互：

- 显示图片。
- 绘制标签标记、气泡和选区。
- 左键/中键拖动画面。
- 标签模式和选区模式。
- 悬停文本提示。
- 只读预览模式。

它持有当前页标签的绘制快照，不直接修改 `Project`。

### `ImagePageCache`

文件：`src/services/ImagePageCache.h`、`src/services/ImagePageCache.cpp`

负责图片加载：

- 按图片路径和目标预览尺寸缓存 `QImage`。
- 异步加载未命中图片。
- 维护 LRU。
- 预加载相邻页。
- 通过 request id 让 UI 丢弃过期结果。

### `ImagePageViewController`

文件：`src/ui/ImagePageViewController.h`、`src/ui/ImagePageViewController.cpp`

负责把 `Project` 当前页、`ImagePageCache` 和 `ImageCanvas` 串起来：

1. 当前页变化时请求显示。
2. 缓存命中则立即 `setImage()`。
3. 缓存未命中则 `setImageLoading()`，等待异步结果。
4. 异步完成后检查 request id、图片路径和当前页是否仍匹配。
5. 安装图片后执行延迟的缩放/视图中心恢复。
6. 预加载前后页。

如果换页性能、缩放恢复或大图加载出问题，优先从这里和 `ImagePageCache` 看。

## 右侧标签列表

### `LabelTableModel`

文件：`src/ui/LabelTableModel.h`、`src/ui/LabelTableModel.cpp`

这是 `QAbstractTableModel`，负责：

- 从当前页标签指针构建可见行。
- 过滤已删除标签和未选中的分组。
- 将内部 `labelIndex` 映射到表格行。
- UI 显示连续 `visibleIndex`。
- 提供文本和类别列的显示/编辑数据。
- 发出编辑和拖拽重排请求。

`sourceIndexForRow()` 和 `rowForSourceIndex()` 是关键函数。凡是表格筛选后还要定位内部标签，都应通过它们。

### `LabelEditDelegates`

文件：`src/ui/LabelEditDelegates.h`、`src/ui/LabelEditDelegates.cpp`

负责表格编辑控件：

- 文本列使用 `QPlainTextEdit`。
- 类别列使用 `QComboBox`。
- 文本编辑器在打开时把光标放到末尾。
- 编辑期间的行高和提交由委托与控制器配合处理。

## 页面顺序与合并窗口

### `PageOrderDialog` / `PageOrderListModel`

用于调整页面顺序：

- 右侧列表显示当前顺序。
- 支持拖拽、上移、下移、删除。
- 左侧用只读 `ImageCanvas` 预览当前页和标签。
- 对最终顺序的校验和应用由 `ProjectPageOrderService` / `ProjectWorkflowController` 完成。

`PageOrderListModel` 使用 `QAbstractItemModelTester` 覆盖模型合约，改动拖拽逻辑时要同步测试。

### `ProjectMergeDialog`

用于解决合并工程冲突页：

- 每个候选工程显示一个只读预览。
- 多个预览同步缩放和视图中心。
- 用户选择每个冲突页采用哪个候选。
- 冲突解决后可继续进入页面顺序调整界面。

## 自动化脚本

### C++ 侧

主要文件：

- `src/ui/AutomationController.cpp`
- `src/services/AutomationService.cpp`
- `src/services/AutomationManifestParser.cpp`
- `src/services/AutomationOperationApplier.cpp`
- `src/services/AutomationPythonResolver.cpp`
- `src/services/SecretStore.cpp`

运行流程：

1. `AutomationController` 发现脚本并构建菜单。
2. 用户点击脚本动作。
3. 如果脚本清单声明了 `parameters`，弹出 `AutomationParameterDialog`。
4. 密钥参数写入系统密钥环，运行脚本前再读出并注入环境变量。
5. `AutomationService` 导出 `input.json`，启动外部 Python。
6. `AutomationRunDialog` 可选显示 stdout/stderr，并允许取消。
7. 脚本写出 `output.json`。
8. `AutomationManifestParser` 解析输出。
9. `AutomationOperationApplier` 校验并规划 `operations`。
10. `MainWindow` 把合法操作作为一个撤销命令应用到工程。

脚本运行期间，项目编辑入口被禁用，但只读浏览操作仍允许。

### Python 侧

官方脚本在 `scripts/official`：

- `sdk/labelqt_automation.py`：脚本开发辅助库。
- `test`：自动化框架示例和测试脚本。
- `ocr_preview`：OCR 配置、预览、生成标签和页码区间 OCR。
- `ai_translate`：DeepSeek 配置、预览、应用翻译和跨页翻译。

Python 脚本不应该直接写 `.txt` 工程文件。要修改工程时输出 `operations`。

## 偏好设置窗口

文件：

- `src/ui/PreferenceDialog.cpp`
- `src/ui/PreferenceDialogWidgets.cpp`
- `src/ui/GroupStyleEditorWidget.cpp`
- `src/ui/AutomationShortcutEditorWidget.cpp`

偏好设置窗口当前只有保存语义：

- 打开时显示当前运行时 `AppPreferences`。
- 点击“保存”写入 `preference.json` 并立即应用。
- 点击 `Reload` 才从磁盘重新读取。

复杂表格子页已经拆成独立 widget。新增偏好页时，不要把大量表格构造和 JSON 转换逻辑直接写进 `PreferenceDialog`。

## 国际化

翻译源文件：

- `translations/labelqt_zh_CN.ts`
- `translations/labelqt_zh_TW.ts`
- `translations/labelqt_ja_JP.ts`
- `translations/labelqt_en_US.ts`

规则：

- 新增用户可见 UI 文本必须 `tr()`。
- 同步四份 `.ts`。
- 运行 `python scripts/check_translations.py`。
- 构建时 `.qm` 会进入资源路径 `/i18n`。

## 测试

当前测试入口是 `tests/LabelTests.cpp`。它覆盖的范围比早期多很多：

- Label 坐标裁剪。
- LabelPlus 读写。
- 偏好解析、默认值和损坏 JSON 回退。
- 自动化快捷键解析。
- 自动化操作的应用和回滚。
- 标签导航。
- `LabelTableModel` 和 `PageOrderListModel` 的模型合约。
- 会话多选恢复。
- 缺失图片扫描。
- 校对基线、工程对比、页内 label 序列 diff、文本 diff 和 HTML 报告导出。
- `ImageCanvas` Ctrl 点击/拖动交互。
- 合并工程、来源注释和页面顺序服务。

GUI 显示问题不一定都能单元测试，但容易回归的模型逻辑和鼠标/键盘状态机应尽量写轻量 Qt Test。

## 修改功能时的落点

- 文件格式能力：`LabelPlusDocument`。
- 项目数据字段：`Project` / `Label`，再同步读写、自动化 JSON 和测试。
- 标签编辑：`LabelEditController`，并注册撤销/重做。
- 页面顺序：`ProjectPageOrderService` / `ProjectWorkflowController` / `PageOrderListModel`。
- 合并：`ProjectMergeService` / `ProjectMergeDialog`。
- 图片加载与换页性能：`ImagePageCache` / `ImagePageViewController`。
- 画布交互：`ImageCanvas` 发信号，服务或主窗口协调数据修改。
- 表格显示和编辑：`LabelTableModel` / `LabelEditDelegates`。
- 选择同步：`LabelSelectionController`。
- 编辑器提交和焦点恢复：`EditorStateController`。
- 主窗口快捷键：`MainWindowShortcutController`。
- 自动化脚本发现/运行：`AutomationController` / `AutomationService`。
- 自动化脚本 JSON 词汇：`AutomationManifestParser` / `docs/automation-scripting.md`。
- 自动化操作：`AutomationOperationApplier`，并补 SDK 示例。
- 校对与工程对比：`ProjectComparisonService` / `LabelSequenceDiffService` / `TextDiffService`。
- 校对 HTML 报告：`ProofreadReportService`。
- 偏好设置：`AppPreferences`、`PreferenceDialog` 和对应子控件。
- 新 UI 文本：`tr()` + 四份翻译。
