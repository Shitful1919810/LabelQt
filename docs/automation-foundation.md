# 自动化基础契约（已实现）

本分支先实现稳定身份、运行期版本、作用域快照、任务依赖和统一提交入口。
现已接入原生 v2 批处理协议与 Python SDK；持续通信/常驻 worker 仍见后续 [API 边界](automation-api-boundaries.md)。

## 模块边界

| 模块 | 职责 |
| --- | --- |
| `core/ProjectIdentityService` | 校验与持久化工程、页面、Label、分组 UUID；复制身份规则 |
| `services/project/ProjectRevisionTracker` | 会话 epoch、单调 revision、字段版本与删除墓碑 |
| `services/project/ProjectController` | 活动工程所有者；发布原生编辑、保存、加载、恢复的版本边界 |
| `services/automation/ProjectTaskService` | 创建任务、只读值快照、宿主依赖记录、权限/冲突校验、取消、单次提交 |
| `services/label/LabelEditController` | 类型化 `LabelChange` 校验、原子批量编辑、一次撤销/重做及编辑通知 |
| `services/automation/AutomationProtocol` | 原生 v2 范围规划、快照编码、严格结果解码，直接产生 LabelChange |

`MainWindow` 不再持有自动化专属的修改和撤销路径。全文替换和自动化批量写入共享
`LabelEditController::commitChanges()`；交互编辑仍保留原有的选择/焦点通知语义。
批量提交的实现集中在 `LabelEditControllerBatch.cpp`，不增加独立的自动化撤销机制。

## 身份与存储

- 新工程、页面、Label、分组获得 UUID。序号、文件名、路径不是身份。
- 排序、重命名、图片重新关联保留 ID；撤销/重做恢复原 ID。
- 新增/粘贴 Label、合并生成的新工程及其实体使用新 ID；普通值复制用于快照，不重新生成 ID。
- 删除分组后重新新建同名分组获得新 ID；撤销删除则恢复原分组 ID。
- ID 写在 `ProjectMetadataService` 管理的压缩 `LabelQtMetadata.identities` v1 section，
  不改变 LabelPlus 正文语法。旧工程加载时生成身份，下次保存时写入。
- 保存只导出非软删除 Label 的身份，并按实际输出序列排列。分组、页面及 Label ID 不得为空或重复。
- 页面持久化名称必须非空且唯一。旧格式文件也在读取时检查；不能先接受歧义页名，再写出无法重新打开的身份表。
- 标签身份列表附有页面内容摘要（文本、分组和按文件精度归一化的坐标）。外部编辑器更改正文而未
  更新元数据时，不再按旧行号强行挂接身份：该页重新生成 Label ID。损坏的有效身份表拒绝加载或保存。
- 另存为保持工程 lineage，但改变运行会话；文件副本不意味着两个打开的文档共享一个可写会话。

## 版本与依赖

运行会话和版本不保存进 TXT，也不随撤销回退。每个有实际变化的检查点增加 revision，受影响字段
记录新版本。删除后的版本墓碑保留到会话结束，避免“删除后撤销”或“改完再改回”复活旧任务。

依赖键为 `ProjectResource { UUID, ProjectField }`，覆盖：

- Label 存在/所属页、文本、坐标、分组、软删除。
- 页存在、页内标签序列、图片名和路径、工程页序。
- 分组存在/名称、分组目录和活动成员集合。
- 工程注释元数据。它仅可作为只读依赖，不能由脚本自由修改。

任务显式声明读取范围 `pages` / `labels` 和写入范围 `writePages` / `writeLabels`。
v2 脚本通过 `scope` 在整个工程、当前页和所选 Label 中选择一种，由宿主规划具体范围。
可写范围必须属于快照范围。另行声明 `readFields` / `writeFields` 和 `allowCreate`。
默认没有任何写权限；读到的参考页并不会自动成为修改目标。

获取一页快照只返回该页数据，获取几个 Label 只返回这些 Label。不是每次都序列化整个工程。
检查点利用 Qt 隐式共享跳过未修改页面的标签数组；修改页扫描其字段，分组成员/结构变化时才重算
成员集合。ID 定位及页面遍历仍可能是线性的，本阶段没有实现变更订阅、增量查询协议和无限历史日志。
版本追踪器独立持有外层页面容器，标签值仍然共享。表格通过只读 provider 重新定位当前页标签，
不保存容器内部地址，因此普通对比快照的写时复制和释放不会破坏表格绑定。

`readPageOrder`、`readGroups`、`readMetadata` 是显式依赖。按页查询自动依赖该页标签序列，
因此页内增删/重排会使旧页快照过期；按 Label 查询不依赖无关标签的位置和数量。
任务读取 `ImageSource` 时，还记录对应文件的 SHA-256，并在提交时复核，防止路径不变但图片被替换。
这一步有实际文件 IO 成本，不读取图片的文本任务不要申请该字段。

版本属于宿主。脚本不能提供一个更高版本、改回旧值或修改回传快照来绕过校验。

## 提交与线程规则

`AutomationController` 从参数/钥匙串准备阶段起占用运行状态并保护工程编辑，不能等进程启动后才上锁。
每次运行独立持有脚本配置、task ID、runner、窗口及取消状态；刷新脚本和重复启动在准备中也被拒绝。
钥匙串内部仍有嵌套事件循环，但取消准备会阻止后续捕获/启动；延迟完成回调只处理其自身运行实例。
所有准备失败路径通过同一退出清理释放状态。

1. 在文档所有者线程提交活动编辑器内容，再由宿主构造授权范围、调用 `capture()`。
2. 向工作线程/进程发送快照值，不发送 `Project&`、Label 指针或窗口指针。
3. 算法返回 `LabelChange` 列表。空 `labelId` 表示新增，ID 由宿主生成；未指定字段保持不变。
4. 用以 `ProjectTaskService` 为接收上下文的 queued 调用将结果交回所有者线程。
5. `submit()` 消耗任务凭证，检查会话、期限、全部读依赖、目标权限、文件摘要和完整变更列表。
6. 全部通过才调用编辑控制器，注册一条撤销记录，然后通知界面。无效项不再跳过后部分执行。

成功返回 `ProjectTaskCommitReceipt`：任务/会话 ID、完成时版本，以及变更和新建 Label 的 ID。
调用方无需猜测新增 Label 的行号；新任务通过 ID 继续查询即可。

现有字段操作包括改文本、分组、坐标、软删除以及新增 Label；不允许 raw Project 替换、
任意元数据写入、保存/重命名/恢复等文件工作流。`commitChanges()` 仅供宿主编辑层使用，外部提交必须
走 `ProjectTaskService`。任务范围和权限必须由宿主批准，不能原样信任脚本自报请求。

线程错误返回 `WrongThread`，不通过加锁让工作线程访问 Qt 模型。校验与变更期间不启动嵌套事件循环。
取消与提交按宿主事件队列顺序处理；取消后迟到的结果、同一任务的第二次提交均拒绝。
以服务 QObject 为上下文的排队调用会在其销毁后失效；调用方仍须负责工作线程/进程的停机和生命周期。

当前界限：最多 64 个存活任务、30 分钟有效期、一次最多 10,000 项变更及 4 Mi 个 UTF-16 文本单元；
进程 runner 的 JSON 文件读取上限为 16 MiB（清单和返回文件均适用）。
失败提交也消耗凭证；要重算或重新审批，重新获取任务快照。

## 既有功能联动

下表描述服务层的依赖校验契约，不表示当前脚本运行界面已开放按页并行编辑。
当前 runner 仍全局保护工程编辑；页面/Label 级并行是解除 UI 保护时必须遵守的条件。

| 同时发生的操作 | 处理 |
| --- | --- |
| 任务改 A 页文本，用户改 B 页 | 无 B 页依赖时允许提交 |
| 任务仅改文本，用户移动同一 marker | 未读取坐标时允许；撤销任务也不回退坐标 |
| 用户修改目标文本后撤销 | 文本版本已经增加，拒绝旧任务 |
| 用户改变参考页文本 | 参考文本若属于读依赖，拒绝提交 |
| 重排/重命名页面 | 按 ID 定位不失效；声明页序/图片来源依赖的任务仍会冲突 |
| 删除目标、改变分组身份、页内结构变化 | 检查对应资源依赖，不对旧下标继续写入 |
| 打开/恢复/替换工程或另存为 | 改变会话，旧任务失效 |
| 保存当前工程、自动备份 | 不改变内容身份；继续沿用现有外部文件冲突与原子写入规则 |
| 修订基线/审阅状态 | 元数据依赖单独跟踪；Label 修改保留软删除与普通撤销规则 |

活动工程的 `LabelEditController` 必须用 `ProjectController&` 构造，直接发布版本，不依赖 UI
回调是否存在。仅接收 `Project&` 的构造用于离线编辑/测试，不能绑定任务服务。
原生页面/元数据写入仍须经所属工作流发布 `markDirty()` 检查点，且撤销和重做都要发布；
不要跨检查点缓存活动工程中的可写元素引用。新增写入路径需要对应 ABA/撤销回归测试。

## v2 脚本与后续阶段

已删除旧 SDK、旧 OCR/翻译脚本、`AutomationOperationApplier`、
`AutomationOperationTranslator` 和 `MainWindow::applyAutomationOperations`。
v2 runner 只接收 `ProjectTaskSnapshot`，不接收或长期持有 `Project`。
脚本清单必须声明范围/字段，结果按 UUID 直接解码为 `LabelChange` 并交任务服务提交。
协议和 SDK 说明见 [自动化开发指南](automation-scripting.md)。

当前仍是独立进程、一次输入/输出文件和全局编辑保护，不提供运行期再 capture、
逐步提交或常驻模型。接下来的独立工作包括 worker/双向 IPC、SDK 打包与工具链、
授权/候选审核 UI、素材固定快照、任务调度和有选择地解除界面编辑保护。
图片摘要复核不等于文件锁，也不是对外部进程任意文件写入的安全沙箱。

## 验证

`AutomationTaskTests` 覆盖 ID round-trip/复制/软删除/外部编辑、作用域与字段冲突、ABA、页面排序、
单次提交/批量撤销、权限拒绝、取消与排队交付、线程归属及旧输出拒绝。
`AutomationProtocolTests` 覆盖 v2 格式、范围和 Python SDK 端到端提交。
另运行 `ProductWorkflowTests`、`PersistenceSafetyTests`、`LabelQtTests` 和校对表格模型测试，
确认恢复、重命名、查找、修订与已有交互没有因身份持久化而回归。

```bash
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
python scripts/devtool.py check --mode fast
```
