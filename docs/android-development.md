# Android 开发与协作约定

本文补充根目录 [AGENTS.md](../AGENTS.md)、[CONTRIBUTING.md](../CONTRIBUTING.md) 和
[架构文档](architecture.md)，适用于人工开发与 Agent。Android 当前是技术预览，不是完整发行版。

## 文档分工

| 文档 | 用途 |
| --- | --- |
| [使用说明](android-user-guide.md) | 只介绍已实现的操作、数据边界与用户可见限制 |
| [构建与测试](android-build.md) | 工具链、命令、存储策略、验证记录与发行门槛 |
| [移植方案](android-port-plan.md) | 完整首版的设计基线，包含尚未实现的能力 |
| [TODO](../TODO.md) | 实施状态；部分能力完成不等于整个里程碑通过 |
| [2026-11-11 审查](android-review-2026-11-11.md) | 特定时间的审查证据，不替代持续回归 |

进入模块前先执行 `python scripts/devtool.py module-map android`，再按
`label-edit`、`selection`、`proofread`、`recovery` 等主题定位共享服务。
修改产品行为时同步使用说明；修改职责、文件协议、构建或资源策略时同步相应开发文档。
不要把规划中的能力写成现成按钮，也不要把主机测试通过写成真机验收通过。

## 职责边界

| 层或模块 | 应负责 | 不应负责 |
| --- | --- | --- |
| `src/core`、共享编辑/校对/持久化服务 | 格式、稳定身份、版本、命令、撤销、保存与校对规则 | Widgets/QML 布局、Android URI |
| `ProjectCreationService` | 按显式图片列表构建未保存工程、共享默认分组与自然排序规则 | 解包、活动文档切换、写盘 |
| `MobileDocument` | 活动文档的移动适配、选择、编辑草稿、自动保存调度 | 第二套编辑命令、ZIP 解析、平台授权 |
| `MobileLabelModel` | 标签展示角色、分组样式映射、稳定增量刷新 | 直接修改正文或保存文件 |
| `MobileReviewController` | 校对命令适配、异步差异展示与过期检查 | 独立实现轮次、批注或文本 diff 算法 |
| `MobileWorkspaceController` / `MobileWorkspaceModel` | 工作区操作编排、任务串行、列表数据 | 归档格式与文件安全规则 |
| `ProjectTransferService` / `TransferArchive` / `TransferSupport` | 导入编排、白名单归档解码、共用路径/IO 校验、快照打包与暂存发布 | 操作活动文档、接受 URI、UI 状态 |
| `WorkspaceStorageService` | 工作区清单、显示名、分享文件名校验、删除与清理策略 | 关闭编辑器、清理撤销栈 |
| `src/mobile/qml` | 呈现、手势、坐标转换、发出命令 | 工程结构修改、业务撤销、磁盘事务 |
| `src/platform/android`、`android/` | Intent、授权流、JNI、FileProvider | 工程解析、编辑、校对或归档规则 |

构建保持 `LabelQtShared`、`LabelQtDesktop` 与移动目标的边界。
优先复用已有服务，确有跨前端规则才抽取共享层；不创建隐藏 Widgets 来复用 UI 控制器，
不把所有移动逻辑塞入 `Main.qml` 或单个控制器。
Android 不构建 Python runner/QtKeychain，但不得破坏桌面自动化及共享身份、版本和提交契约。

## 编辑、异步与生命周期

1. 活动 `ProjectController`、模型与 `UndoStack` 只在所属线程访问。标签/分组编辑走
   `LabelEditController`，校对修改走 `ReviewRoundEditService`，保存走共享工程事务。
2. 后台任务只取得所属线程捕获的值快照。异步 diff 返回时校验文档会话与校对版本；
   切换工程、撤销或新轮次后，旧结果不能覆盖当前视图。不能把行号当作长期身份。
3. 编辑草稿和 IME 预编辑不是已保存正文。切页、切标签、撤销、切工程、分享等入口须遵循
   同一提交流程；提交失败保留草稿并阻止后续破坏性动作。不要为每个按钮另写一次提交逻辑。
4. QML 拖动只更新视觉预览，结束时提交一次命令。模型重建、页面切换、第二指加入、失焦等
   取消路径不得提交半次移动；双指结束后须全部抬指才能开始新手势。
5. 对象销毁前解除内部回调；定时器、worker 和 JNI 的迟到结果不能使用已销毁的前端。
   JNI 回调先排入 Qt 所属线程，不能在 Java 回调线程直接改工程。
6. 工作区导入、导出与维护按服务协调串行执行。导出中的图源必须保持稳定；未来增加
   页面改名、补图或删除时，须扩展资源协调，不能仅禁用某个按钮来保证安全。

当前文本空闲提交定时器为 750 ms，编辑后自动保存定时器为 1,000 ms；这不是持久化时限保证。
后台切换只是尽力保存，不承诺对强杀、掉电或未结束 IME 输入的恢复。
不能把“草稿提交”“本地保存”“ZIP 生成”“交给外部应用”合并为一个成功状态。

## 模型与触控界面

- 首页和工程抽屉共用 `WorkspaceBrowser.qml` 与工作区模型，不复制列表和管理逻辑。
- 同一组可见标签只更新实际变化的 model role；选择或文本修改不能无故 reset model、重建手势目标。
  结构/筛选变化才重建，配合 `QAbstractItemModelTester` 与手势取消测试。
- 分组样式索引来自完整工程分组顺序，不来自筛选后的列表。未配置分组保持共享的回退规则；
  选中强调外圈，不覆盖组色。新增目标取当前实际可见分组，不能使用撤销后已不存在的分组。
- marker 可见尺寸与触摸热区分开；工具图标使用随包资源，不依赖字体中是否有特殊字符。
  交互不能依赖 hover、右键或键盘修饰键。
- 校验失败需要留在原弹窗的操作复用 `SubmitDialog.qml`，只在提交成功后接受并关闭。
  不使用默认 AcceptRole 先关窗再报错。
- 中文 IME、键盘缩小可视区、横竖屏和窄屏都属于必要状态；主机布局截图不足以证明手机输入体验。
- 新文案使用 `tr()` / `qsTr()` / `qsTranslate()`，更新四语言目录并验证运行时翻译上下文。
  新增可配置行为走 `AppPreferences`，不要另设 QML 常量和偏好默认值两套来源。

## 文件与安全契约

**外部授权与内部路径分开。** `content://` 是能力引用，不是本地路径。平台桥把已授权流有界复制到
私有 inbox，业务服务只接收该本地副本。导入不回写原 ZIP/TXT；分享只给 outgoing 中指定文件的只读授权。
不能为绕过厂商文件选择器问题请求全部文件权限、扫描 QQ 私有目录或扩大 FileProvider 根目录。

**输入必须验证。** 归档条目、图片引用、显示名、MIME 和 Intent 都不可信。保留路径越界、链接、
大小写/规范化冲突、格式和资源上限检查；超限拒绝，不截断标签或静默删除未知元数据。
只在启动边界解析可信的系统私有根目录别名，不通过 canonicalization 放行工作区里的不可信链接。
图片引用必须落在 TXT 所在目录下；导出也检查校对前后稿的引用。

**三种名称不可混为一谈。** 工作区 ID 决定身份，`workspace.json` 中的 `title` 是显示名，
`archiveName` 是默认分享名称。重命名显示名不改图片/TXT 或原始分享名。
导入标题取来源显示名的 `completeBaseName()`，去掉最后一个扩展名（`.tar.gz` 作为整体），保留主体中的点和 Unicode；
例如 `chapter.zip` 得到 `chapter`，`chapter..zip` 得到 `chapter.`。
名称异常需检查原文件名、provider 显示名、清单和 UI，不用全局删点或静默迁移旧标题掩盖问题。
非 ZIP 来源的默认分享名使用标题加 `.zip`，不得生成 `.7z.zip` 等误导性名称。

**格式识别与工程选择分开。** 文件头选择唯一归档 reader，不能把“非 ZIP”直接当作 TXT。
一个 TXT 按既有工程导入，多个 TXT 拒绝；零 TXT 才能按支持的图片构建新工程。
工程发现阶段排除 `__MACOSX` 目录和 `._` 资源文件，但不得绕过此前的路径、链接、碰撞及大小校验。
tar.gz 先用 zlib 流式检查完整 gzip 的 CRC/ISIZE，再交给 libarchive 解析 tar；后者本身不校验 gzip 尾部 CRC，
且可能在 tar 结束时提前停止。该预检增加一次解压计算、不增加解压临时文件，展开预算为文件总预算加
每条目 64 KiB 及全局 1 MiB 的容器开销；允许尾部零填充，拒绝拼接 gzip 数据流及非零尾随内容。
图片头部无效时拒绝整次新建，不静默丢页；排序不能依赖 Android 未编入的 ICU。
图片子目录与稳定身份经既有 materialize/序列化流程发布，不能在 QML 中拼 TXT。

**发布与清理分开。** 导入在暂存目录验证完成后发布；同名分享各自使用唯一输出目录，不覆盖旧快照。
清理只针对服务定义的暂存、接收副本、过期分享及已确认删除目录，不涉及工作区、恢复证据或普通备份。
删除活动工程须先提交/保存、成功移出工作区，再停止会话定时器并清除撤销，随后异步删除。
失败不得让旧定时器重新创建已删除工程。保留策略及配额以 [存储说明](android-build.md#存储与清理) 为准。

稳定身份与校对信息仍经统一 `LabelQtMetadata` 写入兼容 TXT，不引入移动专属正文格式。
元数据压缩不是加密，分享不会自动脱敏用户正文或批注。
`TextDiffService` 的安全门控同样适用于 Android，禁止从移动端直接调用已知有风险的 cleanup API。

## 验证与交付

文档或导航调整至少执行快速检查；代码修改按影响范围运行移动主机测试、桌面回归和 APK 构建：

```bash
python scripts/devtool.py check --mode fast
python scripts/devtool.py module-map android

cmake --preset mobile-host-debug
cmake --build --preset mobile-host-debug --parallel 8
ctest --preset mobile-host-debug

python scripts/devtool.py check --mode all --preset linux-debug
```

APK 的环境变量、`android-arm64-debug` 命令与安装方法见 [构建说明](android-build.md)。
修改 QML、资源或 JNI 后需重建 APK，不能只提供旧包。修改文档不必重新打包。

| 修改范围 | 必查回归 |
| --- | --- |
| 编辑、选择、分组 | 文本/位置撤销重做、筛选后新增、撤销分组、桌面编辑与自动化 |
| 模型、触控、布局 | 模型测试、选中不重建、重叠选择、双指结束、拖动中切页、窄屏及键盘 |
| 校对与批注 | 开始/结束/替换轮次、页面/标签批注、正文后续修改、迟到 diff、桌面读取 |
| 传输与工作区 | 不合法 ZIP、原子失败、同名独立导入、活动工程删除、分享配额、清理不误删 |
| 平台桥与保存 | 冷/热启动导入、URI 失效、空间不足、IME 预编辑、后台/杀进程、QQ 双向交接 |

真机记录须注明 APK/提交、签名、设备/ROM、输入法及 QQ 版本；通过项和未测项分开列出。
现有 vivo/iQOO 人工反馈不是完整验收。发生数据异常时保留诊断材料，不能用“清除应用数据”替代修复。

公开分发前检查实际 APK 的签名、Manifest 权限、每个原生库及 ZIP 的 16 KiB 对齐、依赖许可、
对应源码和重建说明。SDK、Qt 二进制、签名密钥和测试用户工程不得提交；Android 不新增 GPL-only Qt 模块。
正式门槛仍以移植方案和 TODO 为准，不因已有可安装 APK 就自动通过。
