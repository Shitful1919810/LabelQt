# 面向第三方开发者的自动化扩展 API 与 SDK 设计

日期：2026-09-06。状态：SDK/协议设计稿；[宿主侧基础契约](automation-foundation.md) 已实现，
最小原生 v2 批处理 SDK 已实现，接口以 [开发指南](automation-scripting.md) 为准。
本稿的持续通信/profile/审核 SDK 示例仍为后续设计，不是现行接口。

本文细化 [自动化功能演进分析](automation-evolution-proposal.md)，重点讨论“用户能够自行开发扩展”这一要求。下文的类型、清单、命令、目录和 Python 示例均为拟议 API，不是当前版本已经提供的功能；现行接口仍以 [脚本开发指南](automation-scripting.md) 为准。

第二轮从零推演后的核心决策见 [API 边界、运行位置与工程访问模型](automation-api-boundaries.md)。本稿已同步：采用进程外可复用 worker，允许持续获取新快照与受限事件订阅，扩大领域 API，并将“统一提交校验”与“每次必须弹窗”分开。

## 1. 设计结论

应该提供一套 **语言无关的扩展协议、类型化 Python SDK、原生审核界面和开发工具链**。它们共同构成扩展能力，不能只把现有 JSON 字段包装成几个 Python 函数就称为完整 SDK。

最重要的取舍：

1. 插件看到的是授权范围内的不可变数据与不透明目标句柄，不是宿主的 `Project`、`Label*` 或 QWidget。
2. 插件提交的是候选或变更提案，不是直接改对象；主程序统一批准策略、冲突检查、提交与撤销。已获用户单次执行许可的窄范围宏可免二次弹窗，不能免完整性校验。
3. 常见 OCR、翻译、检查和文本处理有专用类型契约，作者不用理解整个工程的操作协议。
4. 自定义批处理另有领域提案 API；保存、导出、页面文件操作等通过专用工作流请求，不因此获得任意写文件或调用宿主函数的权限。
5. 官方能力和第三方能力遵守相同的输入输出语义与提交校验；官方后端可以用 C++ 原生实现，不必为形式统一也绕一遍 Python/JSON。
6. Python SDK 优先做好，协议允许其他语言通过独立可执行程序接入；首版不维护 C++ 二进制插件 ABI，也不同时维护多个语言 SDK。
7. 同一扩展会话可以包含多个短任务，显式取得新快照并请求阶段提交；快照不自动变成实时对象，也不将一个事务锁跨越模型调用或用户确认。

**对作者简化的是接入成本，不是把宿主需要承担的一致性责任转移给作者。**

## 2. 区分能力提供者和自定义命令

### 2.1 能力提供者：接入软件的既有工作流

例如作者只想接入另一家翻译服务，应该实现一个翻译处理函数，随后出现在软件“翻译后端”列表中，自动获得选中标签/页范围、参数、进度、候选对照、应用和撤销。

建议首批契约称为 profile，每个 profile 有独立版本：

| Profile | 输入 | 输出 | 宿主提供的体验 |
| --- | --- | --- | --- |
| `text.transform@1` | 目标文本、参数 | 每目标的文本候选/不变/错误 | 批量文本处理与差异预览 |
| `text.translate@1` | 目标原文、语言、有限上下文、术语 | 每目标的译文候选、警告、实际用量（可选） | 原译文对照、重译、审核应用 |
| `image.ocr@1` | 标准化图像素材、可选区域、语言/方向提示 | 每输入的文字区域、文字与可选置信信息 | 原图叠加框、顺序调整、选择生成标签 |
| `project.check@1` | 目标快照、规则参数 | 定位到目标的诊断，可附有限修正提案 | 问题列表、跳转、选择修复 |
| `project.action@1` | 授权快照、参数 | 明确列出的标签变更提案 | 自定义批处理审核 |

相同“运行与提交”基础，不等于所有结果都应该是 `dict[str, Any]` 或“万能 operations”。翻译后端只返回译文，不应该自己决定删除 Label；OCR 返回区域，不应该自己用页名和数组下标拼 `addLabel`。

### 2.2 自定义命令：保留真正的扩展空间

例如“按某种规则重新分组并微调 marker”属于 `project.action@1`，可以在自动化菜单中出现单独命令。使用与提供者相同的任务、授权、参数、日志、草稿、提交系统，不再另起脚本执行体系。

一个扩展包可以贡献多个提供者和命令。命令名是显示文本，稳定 contribution ID 才用于快捷键、设置和工作流引用。改显示名称或切换翻译不能改变 ID。

扩展形式分层，而不是开放程度只有全有或全无：

| 作者想做的事 | 建议形式 |
| --- | --- |
| 换一个 prompt、模型名或参数组合 | 基于已支持后端的声明式预设，不必写 Python |
| 增加翻译服务、OCR 引擎、检查规则 | 实现对应 profile 的处理函数 |
| 任意组合现有标签操作 | 自定义 action + 类型化变更提案 |
| 新建工具栏、任意 Qt 控件、改磁盘图片 | 不属于首版接口，另行设计与审查 |

Batch job、Interactive session 和 Observer 是执行模式，不是三套标签模型或提交协议。普通提供者默认 batch；交互会话允许新快照和独立阶段回执，Observer 默认只能发布诊断。其授权、事件与阶段语义见第二轮决策稿，不能通过一个 `background=true` 同时获得读写和自动运行许可。

预设只允许填写已定义的参数与消息模板，不开放可执行表达式、任意 HTTP 动作模板或隐式文件访问。否则“无代码预设”仍会变成难以审计的程序。

## 3. 协议、SDK 和内部代码不能混成一层

```text
第三方 Python 函数                 其他语言可执行程序
       |                                  |
类型化 SDK（对象/运行器/开发工具）         协议客户端
       +---------------+------------------+
                       |
             扩展协议 + 数据 schema
                       |
             宿主适配与任务协调服务
                       |
              自动化草稿 / 统一审核
                       |
               标签编辑层 + UndoStack
```

三种稳定性要分别维护：

- **公开协议**：字符串字段、错误码、版本协商、消息顺序、目标句柄及结果语义，不能跟着 C++ 成员改名。
- **公开 Python API**：类型、函数、异常、运行钩子和配置访问方式，需要类型提示、文档和兼容测试。
- **内部实现**：可以重构窗口、缓存、数据容器及服务，只要前两者的可观察行为不变。

Python SDK 不应从 `src/` 自动暴露所有类，更不能要求安装 PySide/PyQt 才能访问工程。初版常用路径是普通 Python 数据对象和函数；图片解码、HTTP 客户端等作为可选依赖，不给纯文本扩展强装模型环境。

## 4. 核心数据模型

### 4.1 快照和引用

| 类型 | 含义与约束 |
| --- | --- |
| `TaskId` / `AttemptId` | 宿主生成的任务与执行代次；重试有新代次 |
| `SnapshotRef` | 当前任务所见的不可变数据版本，不是磁盘文件路径 |
| `PageRef` / `LabelRef` / `GroupRef` | 宿主签发的不透明句柄，只能在相应快照与授权范围中使用 |
| `LabelSnapshot` | 只读文本、分组、归一化 marker 等已授权字段；显示序号仅作显示 |
| `ImageAssetRef` | 任务素材句柄，关联实际像素尺寸、方向、摘要与坐标约定 |
| `InputItemId` | 本次 profile 输入单元，如一个翻译目标或一张 OCR 页面 |
| `CandidateId` / `ProposalId` | 候选和提交提案身份，不等于标签身份 |

Python 对象建议是不可变数据类/只读集合。不可变是 API 语义，不是对恶意 Python 的安全隔离；宿主仍要验证全部返回数据。

所有句柄在协议中使用字符串，不利用 JSON 浮点数传递身份。未授权字段与正常的空值必须区分：profile 的必需字段不足就拒绝启动，可选字段显式缺失；SDK 访问未授权数据报错，不用空文本或 `(0, 0)` 冒充真实值。

对外不使用 `pageName + labelIndex` 作为长期句柄。从零设计时，稳定 page/label/group ID、会话 epoch 与单调 revision 应作为基础能力一并建立，重排不改变对象身份。当前已删除旧 API 和转换器，只接受稳定 ID；旧工程在加载时生成身份。

持久任务恢复是另一项能力：没有可靠身份迁移时，重启后的候选只能查看/复制，不能按当前同名页同序号自动应用。即使未来持久 ID 已有，外部工具重写并丢弃 metadata 后也必须重新验证，不猜配。

### 4.2 目标与上下文必须分开

`request.targets` 是允许产生候选的对象；`request.context` 是辅助算法理解的只读材料。翻译用到邻近标签并不意味着可修改它们。

扩展声明支持哪些范围，用户选择实际范围，宿主截取快照。插件不能用 `scope="project"` 自行扩大范围，也不能把空选中解释为全选。会话可在既有授权内显式 `capture` 新版本；扩大范围需重新授权。新快照不改变旧任务目标，也不在后台悄悄刷新“当前选择”。

大输入按快照分页：分页游标绑定快照、排序和授权，不是随翻页变化的实时查询。普通小任务可以直接携带输入，作者不应被迫为十条文本手写分页循环。

### 4.3 文本和图像的跨语言约定

- 文本使用 Unicode 字符串，不隐式 trim、规范化或把两个字符 `\n` 转成真实换行。清空文本必须是显式 `""`，缺字段和 `null` 都不是清空。
- 公共修改 API 首版使用完整文本替换，不暴露 Qt UTF-16 偏移。检查器若需要定位子串，使用明确的 Unicode code point 半开区间；SDK 提供转换辅助，宿主负责映射 Qt 坐标，并用 emoji 测试验证。
- 原始换行规范化、代理字符处理等必须成为协议规定，不能由不同 SDK 各自猜。建议输入拒绝非法 Unicode 标量，保留合法正文原样。
- OCR 图像素材先由宿主统一方向；区域坐标约定为素材左上原点、x 向右、y 向下的 `[0, 1]` 归一化坐标。输出必须附素材句柄，宿主掌握到原图的映射，避免重复裁剪偏移。
- 字框、多边形、阅读顺序和 marker 是不同概念。没有识别置信度就返回缺失，不用 `1.0` 填充。
- 路径不是素材 API。`ctx.assets.materialize(ref)` 只提供任务内只读约定的素材副本，限时存活；不是对原图的任意路径访问。

## 5. 变更提案 API：易用但不能绕过提交

### 5.1 基础操作与领域扩展

`project.action@1` 的基础标签提案构造器包括以下操作，但不将它们视为整个平台的最终 API 上限：

| 拟议方法 | 语义 |
| --- | --- |
| `proposal.set_text(label, text)` | 对快照中的目标提出完整文本替换 |
| `proposal.set_group(label, group_ref)` | 切换至已存在且授权的分组 |
| `proposal.set_position(label, point)` | 修改 marker，不修改图像 |
| `proposal.delete_label(label)` | 请求删除，实际软删除/修订行为由宿主规则决定 |
| `proposal.add_label(page, text=..., group=..., position=...)` | 新增标签；返回草稿临时句柄，不返回真实数组下标 |

操作需要对应的 `propose` 授权。不开放 `save_project()`、`clear_undo()`、任意 metadata 写入、覆盖图片、任意文件重命名或 `invoke_main_window()`。

完整领域面还应有分组管理、标签/页面排序、工程内页面成员变更和有配额的扩展 metadata 事务。保存、导出、页面改名/指定来源及基线操作通过 `workflows.plan/request_execute` 走宿主确认与持久化规则；这里禁止的是原始无限制写入口，不是这些用户需求。具体方法与风险分级见 [API 清单](automation-api-boundaries.md#5-建议-api-面)。

同一 builder 内对同一字段的多次赋值按调用顺序折叠成一个最终值；序列化后的协议提案不允许同字段重复操作。删除与修改同一目标是冲突，不能靠宿主偶然的 apply 顺序决定结果。新增后的草稿标签修改折叠到新增项，新增后又删除则不产生操作。

初版不允许提案之间引用临时标签，也不支持循环依赖；顺序工作流由宿主持有同一草稿，在阶段间生成新快照，避免插件自行维护跨任务下标。

### 5.2 builder 自动收集前提，宿主作权威校验

作者不必手写哈希算法。SDK 从 `LabelSnapshot` 附上 snapshot/target 引用，宿主从自己保留的快照生成与校验原值和依赖。插件回传的哈希/旧值不是权威依据。

提交流程：

1. 结果完成并进入宿主草稿；用户看到完整修改范围与错误。
2. 用户勾选/修改候选。选择集合形成不可变的提交计划，不把点击时的实时表格行号当目标。
3. 提交活动编辑器，验证工程会话、目标、旧字段、图片与必要上下文依赖。
4. 对确认集合完整规划，失败则不写入；允许排除冲突项时必须重新展示实际范围并由用户确认。
5. 通过统一标签编辑服务应用，一次确认对应一次撤销。来源 metadata 如需同步，也属于同一语义事务。

默认每个 profile 输入项是可独立审核的单位。确需多个操作一起成立的 action，可以声明一个不可拆分的 `ChangeGroup`，UI 只能整组勾选。首版禁止不同组写同一字段，不引入通用依赖图求解器。

插件没有无条件 `commit()`。SDK 中 `publish()` 或函数返回值只意味着产生候选，不意味着已修改或保存工程。交互会话可调用 `proposals.request_apply`，由宿主根据用户许可决定直接应用、等待审核或拒绝，并返回明确回执。用户主动触发且预先批准的窄范围宏可以免二次弹窗；AI、大范围/结构或磁盘修改需要相应审核。不能由插件设置 `quiet=true` 或自称“确定性”开启自动应用。

## 6. Python SDK 应该长什么样

建议发行包名暂定 `labelqt-sdk`，导入名 `labelqt_sdk`；命名需在正式发布前确认可用性。下面示例表达目标使用体验，依赖这份提案中的 API，当前不可直接运行。

三个示例属于不同 contribution，分别需要对应 profile、参数和授权清单；第 7 节只展示文本处理的最小清单。翻译中的服务函数由作者实现，自定义命令片段省略相同的 Extension 初始化代码。

### 6.1 最简单的文本处理

```python
from labelqt_sdk import Extension, TaskContext
from labelqt_sdk.text import TextRequest, TextResult

extension = Extension.from_manifest("labelqt-extension.json")

@extension.handler("trim_trailing_spaces")
def trim_trailing_spaces(ctx: TaskContext, request: TextRequest) -> TextResult:
    result = TextResult.for_request(request)
    for target in request.targets:
        ctx.check_cancelled()
        result.suggest(target, target.text.rstrip(" "))
    return result

if __name__ == "__main__":
    extension.serve()
```

这里作者没有写 JSON、页名、labelIndex、GUI、撤销或文件保存。与原文相同的候选被识别为不变；只删除末尾半角空格是此示例的明确规则，不是 SDK 擅自规范化文本。

`serve()` 的运行器处理握手、分帧、取消、异常和结果提交。profile 与显示信息由清单定义，装饰器只绑定 contribution ID，不重复声明第二套输入输出 schema。

### 6.2 翻译提供者

```python
from labelqt_sdk import AsyncTaskContext, Extension
from labelqt_sdk.translation import TranslationRequest, TranslationResult

extension = Extension.from_manifest("labelqt-extension.json")

@extension.handler("translate")
async def translate(ctx: AsyncTaskContext,
                    request: TranslationRequest) -> TranslationResult:
    # call_my_service 是作者的适配函数，不是 SDK 内置服务。
    items = await call_my_service(
        texts=[{"id": t.item_id, "text": t.text} for t in request.targets],
        target_language=request.target_language,
        context=request.context,
        settings=ctx.settings,
        api_key=await ctx.secrets.get("provider"),
    )
    result = TranslationResult.for_request(request)
    for item in items:
        target = request.require_target(item["id"])
        result.suggest(target, item["translation"])
    return result
```

示例中的响应映射、请求大小、超时和具体网络库由作者适配。SDK/宿主双重检查遗漏、重复、错误类型和未知 ID；不会替作者将错误响应猜成正确译文。`suggest` 首版提供每目标一个候选；重译产生新 attempt，宿主保留旧候选。多候选排序可作为后续 profile 小版本能力协商。

密钥读取必须在用户批准的 contribution、任务和凭据配置内；`provider` 是逻辑名称，不是任意系统密钥环 service/account。返回给 Python 后就是该受信任扩展能读取的明文，不能宣称 SDK 可以阻止其泄露。

### 6.3 较复杂的自定义命令

```python
from labelqt_sdk import ActionRequest, ChangeProposal, TaskContext

@extension.handler("move_and_regroup")
def move_and_regroup(ctx: TaskContext,
                     request: ActionRequest) -> ChangeProposal:
    proposal = ctx.proposal()
    group = request.require_group(ctx.parameters["target_group"])
    for label in request.targets.labels:
        ctx.check_cancelled()
        proposal.set_group(label, group)
        proposal.set_position(label, label.position.translated(dx=0.01, dy=0))
    return proposal
```

超出 `[0, 1]` 的位置产生明确校验错误，不静默 clamp 改变作者意图；作者想要截断时调用显式 `point.clamped()`。参数中的 group 是宿主下拉选择形成的句柄，不是任意字符串绕过验证。

### 6.4 长任务和模型生命周期

- 同时支持普通 `def` 和 `async def`，不用为简单函数强学 asyncio；普通函数接受 `TaskContext`，由 SDK 在任务执行线程运行，控制通道保持可响应。异步函数接受 `AsyncTaskContext`，需要宿主往返的同名方法具有明确的异步签名；共用数据类型，不让一个方法根据运行环境随机返回值或 coroutine。
- 每个 worker 初版只并发运行一个处理函数；SDK 的宿主请求与日志 API 负责跨线程封送，作者不能直接操作协议流。
- 提供 `prepare(LoadContext)` / `close()` 钩子缓存模型；模型不在扫描插件目录时加载。LoadContext 只有非敏感加载设置，不含活动工程或任务密钥。
- 分页任务可用同步上下文的 `ctx.publish(result_batch)` 或异步上下文的 `await ctx.publish(result_batch)` 提前发布完整输入项结果，其返回表示宿主已接收候选，不是工程已应用。已发布项不能在函数最终返回值中再发布一次；SDK 统一记录已完成项，最终返回只携带剩余结果，并检查整个 request 的覆盖情况。
- `ctx.check_cancelled()` 进行协作取消；原生推理阻塞或扩展不合作时，宿主可在超时后终止整个 worker，不能尝试强杀 Python 线程继续复用模型。
- `ctx.progress(completed, total, stage=...)` 与 `ctx.log(...)` 分开。总量未知可省略 total，不把日志文本中的百分比当进度协议。
- 可取消只说明本地任务响应取消；不保证远端服务停止计算，也不保证外部收费请求没有执行。

SDK 不应缓存原始 `TaskContext` 到下次任务；运行器每次创建新上下文并在结束后撤销其宿主访问。较长期的 `SessionContext` 持有有限会话服务，不持有实时工程对象；其下的每个任务和快照仍有独立生命周期。模型缓存按扩展版本、非敏感模型配置和设备隔离，切换凭据授权边界时重启 worker，避免上一个任务的秘密残留在复用实例中。

## 7. 清单和配置：一处声明，多处复用

建议新清单为 `labelqt-extension.json`，与旧 `script.json` 区分。以下是前述文本处理的完整最小设计示例：

```json
{
  "manifestVersion": 1,
  "id": "io.example.text-tools",
  "version": "1.0.0",
  "name": {"en-US": "Text Tools", "zh-CN": "文本工具"},
  "license": "MIT",
  "protocol": {"major": 2, "minMinor": 0},
  "runtime": {"kind": "python", "entry": "main.py"},
  "contributions": [
    {
      "id": "trim_trailing_spaces",
      "profile": "text.transform@1",
      "title": {"en-US": "Trim trailing spaces", "zh-CN": "移除末尾空格"},
      "scopes": ["selectedLabels", "currentPage", "pageRange"],
      "access": {
        "read": ["labels.text"],
        "propose": ["labels.text"],
        "network": []
      },
      "parameters": {"type": "object", "properties": {}, "additionalProperties": false}
    }
  ]
}
```

`io.example` 只是示例命名空间；倒置域名并不证明发布者身份。实际安装身份还需绑定用户信任的来源与包摘要，不能让另一个同 ID 包自动替换并继承凭据。

以下三件事不要都叫 capability：

- **Profile**：实现什么类型的功能，例如翻译。
- **Features**：双方支持的协议扩展，例如分项发布、可选诊断区间。
- **Access grants**：用户准许此次运行读什么、产生什么提案、使用哪个网络目的地/密钥。

清单是静态声明上限，握手返回实际能力，最终 task envelope 给出实际授权；三者取交集。动态模型/语言列表可以在启用后查询，但不能在握手里新增未声明的权限。

### 7.1 参数与设置分层

| 类别 | 示例 | 所有者 |
| --- | --- | --- |
| 工作流参数 | 页范围、目标语言、输出分组 | 宿主 profile 表单 |
| 扩展特有参数 | 某条规则开关、模型特有提示 | 清单 parameters，由宿主生成表单 |
| 持久非敏感设置 | 模型目录、服务地址、设备 | 用户级扩展配置，版本化 |
| 密钥 | API key | QtKeychain，单独授权引用 |
| 临时/缓存数据 | 模型缓存、任务素材 | 宿主管理的扩展命名空间目录 |

范围、语言等标准字段不能在每个翻译扩展里重新声明一份，否则统一界面仍会退化成脚本菜单。扩展只补充特有参数。UI 提示如多行输入、文件选择、分组选择由有限 `ui:*` 注解表达，不允许清单嵌入任意 Qt/QML/HTML。

采用 JSON Schema 2020-12 的明确受支持子集表达参数和协议形状；版本、必填、枚举与未知字段行为由宿主验证。JSON Schema 解决结构约束，不验证目标授权、工程旧值和标签操作冲突。[JSON Schema 规范](https://json-schema.org/draft/2020-12)

运行时不下载任意远程 `$ref`，资源必须随包提供并限制路径、引用深度和大小。未支持的 schema 关键字不能静默忽略后声称已验证；开发工具在安装前给出错误。

UI 文案使用语言映射和明确回退顺序，不依赖脚本文件夹名；宿主语言码与清单 BCP 47 标签统一转换。插件文案不需要让主仓库为每个插件增加 `.ts` 词条；宿主通用错误和控件仍走现有 i18n。

### 7.2 静态发现，不执行发现

菜单构建与安装检查只读清单、schema 和包元数据，不 import Python、不调用任意 `describe()` 代码。作者的初始化代码可能加载模型或访问网络，不能在打开菜单时执行。

启用后才在工作进程内加载入口，核验清单的 contribution ID 与 SDK 注册处理器一致。Python entry points 可以用于包内入口约定，但不能让宿主扫描系统 Python 所有已安装包并加载它们。[PyPA entry points 规范](https://packaging.python.org/en/latest/specifications/entry-points/)

## 8. 通信协议：可互操作，不只是“发点 JSON”

### 8.1 协商与方法边界

当前批处理协议主版本为 2，旧 `apiVersion: 1` 明确拒绝，不提供兼容适配器。清单版本、SDK 版本、profile 版本与协议版本不是一个数字。

采用 JSON-RPC 2.0 的请求/响应封装及通知语义，方法名使用 `initialize`、`task.start` 等应用命名，不占用保留的 `rpc.*`。协议传输、任务语义与授权是 LabelQt 自己的约定。[JSON-RPC 2.0 规范](https://www.jsonrpc.org/specification)

| 方法 | 方向 | 规则 |
| --- | --- | --- |
| `initialize` | 宿主 → worker，请求 | 协商版本、features、profile、限制和配置摘要；不携带工程数据 |
| `task.start` | 宿主 → worker，请求 | 返回接受/拒绝；接受不是计算完成，也不是工程修改完成 |
| `task.progress` | worker → 宿主，通知 | 可合并/丢弃的显示性进度，不承载唯一结果 |
| `task.publish` | worker → 宿主，请求 | 发布完整输入项结果批次；宿主校验并保存草稿后确认 |
| `task.finish` | worker → 宿主，请求 | 最终统计与结局；所有发布先获确认，缺失输入项不能默认为成功 |
| `task.cancel` | 宿主 → worker，请求 | 确认收到取消，不等于已停止；待明确终态或限时终止 |
| `snapshot.read` / `assets.materialize` | worker → 宿主，请求 | 仅访问当前任务的冻结快照和素材 |
| `project.capture` | worker → 宿主，请求 | 在绑定会话与既有授权内取得显式新版本，不能切换到另一个当前工程 |
| `project.subscribe` / `changes_since` | worker → 宿主，请求 | 有界提交事件与游标；失效要求重同步，不提供无限实时对象流 |
| `proposals.preview/submit/request_apply/status` | worker → 宿主，请求 | 预览、候选登记、批准请求与真实回执；没有绕过版本校验的 commit |
| `workflows.plan/request_execute` | worker → 宿主，请求 | 领域级保存/导出/文件等操作，独立审批与副作用边界 |
| `secrets.get` | worker → 宿主，请求 | 仅获得用户已批准的逻辑密钥；不得列举密钥环 |
| `shutdown` | 宿主 → worker，请求 | 正常释放资源；异常退出仍由宿主兜底 |

不提供共享可写实时工程对象、订阅每次鼠标移动、任意文件 IO 代理或让 worker 调用另一个 worker 的接口。新快照、有限提交事件、阶段提案与宿主工作流请求属于允许的持续交互能力；不同能力独立协商和授权。任务组合由宿主显式编排，不通过嵌套脚本运行制造隐式依赖。

### 8.2 请求 ID、发布序号与幂等

JSON-RPC 请求 ID 只关联一次请求响应；它不等于 TaskId，也不能解决重试幂等。双方请求 ID 使用各自前缀字符串，避免双向请求碰撞。

`task.publish` 带 `(TaskId, AttemptId, sequence)` 和候选身份。宿主按连续序号确认：同序号同内容可返回既有确认，同序号不同内容是协议错误。发布大小/未确认数量受握手协商的上限约束，SDK 承担背压与分批，不能无限发送把 GUI 内存塞满。

一个输入项在同一 attempt 内只最终发布一次；多次 token 流不直接替换已供用户编辑的候选。首版可以只流式显示进度、按完整标签/页面发布结果，后续把临时译文预览作为可协商的非提交事件。

若提供“已持久化”确认，宿主必须先原子保存版本化任务草稿再响应；磁盘满返回明确错误，不能发送假确认。持久化在应用数据目录，不是工程 TXT 自动保存。

这些规则保证同一次宿主会话内不会因为重复消息重复新增标签，不承诺远端 API 的 exactly-once 执行。程序在“工程已应用但记录未更新”之间崩溃时，重启必须重新核验工程，不能凭记录缺失自动再应用。首版不自动恢复执行或自动回放提交。

### 8.3 两个状态机

计算状态与草稿状态分开，避免取消、关闭窗口和应用互相覆盖：

```text
计算：Accepted -> Running -> Succeeded | Partial | Failed | Cancelled
草稿：Collecting -> Reviewable -> Applying -> Applied
                          |          |
                       Discarded   Conflict -> Reviewable（重新确认）
```

终态之后不能继续发布本 attempt 的候选。取消与完成竞态由宿主串行裁决；取消生效后拒绝新发布，之前确认保存的完整候选可以保留，但必须由用户明确选择审核，不能自动应用。

默认 batch 在计算终态后提交。交互会话或显式分段任务可以分阶段应用，每段有独立输入、批准、短事务与回执；旧快照不自动刷新，取消后续阶段不撤销已应用段。计算期间允许用户编辑的前提是底层版本与冲突验证已具备，这应作为新设计的基础，而非由插件要求“解除锁定”。用户需要整批一次撤销时延后整体应用，不跨越人工编辑保持一个长期 UndoStack macro。

`Applied` 不表示已保存到磁盘。普通撤销由宿主处理；撤销/重做不重新调用插件。首版已应用草稿不提供重复应用按钮，用户要再次执行必须发起新任务或使用普通重做。

### 8.4 结果与错误

profile 按输入项返回 `success`、`unchanged`、`skipped` 或 `error`，其中 skipped 必须有原因。OCR 一页识别出零区域可以是成功，不等于协议结果缺失。结果数量规则按 profile 定义，不把 OCR 的多框输出误当重复目标。

错误分三层：

- 传输/协议错误：非法 JSON、未知方法、类型错误、消息超限。使用 JSON-RPC 错误结构。
- 执行错误：依赖缺失、鉴权失败、限流、模型不可用、某项计算失败。带稳定 code、可本地化参数、可选可重试提示；宿主决定是否重试。
- 提交错误：目标失效、原文被改、权限不足、重复操作。来自宿主，不伪装成模型调用失败。

统一 SDK 异常可先提供 `ConfigurationError`、`DependencyError`、`ProviderError`、`TaskCancelled`。未知异常转换为执行失败，摘要与诊断分离，日志脱敏；不能写一条“失败”结果消息后仍发送 success。

### 8.5 传输细节与安全上限

默认 `QProcess` stdio，UTF-8 单行 JSON；可读性好的文档样例允许多行排版，线上的一帧只有一行。SDK 处理分片、粘包、CRLF、缓冲区上限和进程退出，不由每个作者实现。

JSON-RPC envelope、数值范围、字符串/候选数量、图像像素量、超时与发布额度都需有限上限。首版是单请求对象的受限传输 profile，不支持 JSON-RPC batch；这是明确的 LabelQt 限制，不能对外宣称支持 JSON-RPC 的所有可选用法。

stdout 保留为协议通道。SDK 在导入扩展前保存协议写端，并将普通 stdout 及原生库输出重定向到 stderr；无法可靠约束的模型可换专用 IPC 适配器。协议并不因此暴露新的业务 API。

## 9. SDK 配套不只是一个包

### 9.1 建议交付物

```text
automation-api/
  spec/                    协议、profiles、生命周期与错误语义
  schemas/                 版本化静态 schema
  fixtures/                正例、反例、消息序列与兼容性样本
sdk/python/
  pyproject.toml
  src/labelqt_sdk/          公共类型、运行器、宿主客户端、开发 CLI
  tests/
examples/extensions/
  text-tools/              无网络、无模型的最小示例
  demo-translation/        假服务驱动的翻译示例
  demo-ocr/                固定识别结果的区域示例
```

目录为建议，不在本轮创建。协议与 SDK 在同一仓库开发、可分别发行；不要把 SDK 长期放在 `scripts/official` 下供用户跨目录 `sys.path.insert()`。

Python SDK 使用标准 `pyproject.toml` 打包与类型标记 `py.typed`，提供可离线安装的 wheel。HTTP/Pillow/模型依赖按功能安装，基础文本 SDK 不依赖 Qt 或 AI 框架。Python 版本与依赖约束用标准包装规范表达，不自造解析器。[PyPA 项目元数据](https://packaging.python.org/en/latest/specifications/pyproject-toml/)、[版本约束规范](https://packaging.python.org/en/latest/specifications/version-specifiers/)

当前仓库为 MIT；建议 SDK 和原创示例也明确采用 MIT，并带完整许可证文件。具体模型权重、第三方客户端和后端二进制的许可证另行核验，不能沿用 SDK 许可证推断它们也能自由分发。

### 9.2 作者开发闭环

拟议命令，不是当前已存在的 `devtool.py` 子命令：

```bash
labelqt-extension init my-tool --template text-transform
labelqt-extension validate ./my-tool
labelqt-extension test ./my-tool --fixture ./samples/selected-labels.json
labelqt-extension run ./my-tool --fixture ./samples/selected-labels.json
labelqt-extension pack ./my-tool
```

- `init` 生成清单、类型化代码、README、许可证占位和测试；模板与发布 SDK 同版本验证。
- `validate` 不执行插件，检查清单、schema、路径、贡献 ID、版本、权限与包资源。
- `test` 在隔离的受管工作目录运行测试宿主，验证输入输出契约和消息序列；仍属于执行受信任代码，不是恶意代码沙箱。
- `run` 使用合成/脱敏 fixture 展示结果预览，不默认修改真实工程或访问真实密钥。
- `pack` 产出版本化扩展包和文件清单，不将作者机器里的虚拟环境、缓存、API key 或绝对路径打入包。

宿主提供开发者模式：选择一个本地扩展目录、显式信任、查看协议/任务诊断、重启扩展。不在每次保存文件时自动对活动工程执行插件；有运行任务时不热替换入口。

fixture 默认从合成数据开始。导出真实工程 fixture 必须确认稿件和图像是否包含隐私；省略密钥并可移除绝对路径。错误复现包不能自动附带整个作品或环境变量。

### 9.3 一份契约，两边验证

schema 与规范是公开字段的单一来源，Python 类型和 C++ DTO/解析器必须通过同一组 fixture。可从 schema 生成简单字段层，但不要求代码生成器推导撤销、权限或事务规则。

如果 C++ 没有 JSON Schema 验证器，选择成熟库前先评估许可与静态部署；也可对小型固定协议编写严格解析器并用共享 fixture 验证。无论哪种，都不在 C++ 与 Python 各写一套互相不对照的默认值。

作者 CI 不需要 Qt GUI、GPU 或真实付费账号即可完成 SDK 契约测试；但 UI/撤销/文件安全仍由宿主 C++ 测试覆盖，不能只测 Python 后宣称扩展不会伤害工程。

## 10. 版本兼容与发布

分别版本化：扩展包、清单格式、传输协议、profile、Python SDK，以及任务草稿存储格式。用户通常只看扩展包版本，开发者诊断显示协商结果。

- 扩展包和 SDK 的公开接口采用语义化版本约定；Python 安装器仍按 PyPA 规则解析依赖。
- 协议/profile 同一主版本只能做可兼容增量；修改字段含义、坐标约定、默认提交语义或移除字段必须换主版本。
- 发布前处于草案阶段的 API 明确标 experimental，不能一边允许生态依赖、一边不说明破坏性修改。
- 握手选择双方可用版本和 features。未知必需 feature 拒绝运行；未知可选 feature 不启用，不能降级成未经审核的写入。
- 字段兼容要有位置规则：操作、权限和必需语义对象严格拒绝未知字段；可扩展诊断信息仅在明确扩展容器中忽略未知内容。不要全局 `additionalProperties=true` 来换兼容。
- 新增可选语义也要被旧消费者安全忽略；否则通过 feature gate 发送。固定 `major/minMinor` 只是起点，最终需配版本协商测试矩阵。

语义化版本的前提是明确公开 API，本设计将字段、生命周期与错误语义都计入该承诺，而非只看 Python 函数是否存在。[Semantic Versioning](https://semver.org/)

本次分支为明确的不兼容重建，仅支持 v2；未来协议废弃应提供迁移指南和诊断，不引入按旧行号猜测目标的适配。

## 11. 安装、权限与发行边界

### 11.1 安装包

初版不必做在线商店。支持“从本地包安装”和“加载开发目录”，把发布者、版本、来源、权限、模型下载大小、受支持平台显示清楚。

扩展包包含清单、代码/入口、声明资源、schema、许可证与依赖描述；Python wheel 或经审核平台后端可作为载荷。不要自制 Python 包解析器，也不要运行任意 shell 安装钩子。源包构建和依赖安装本身可执行代码，批准信任必须发生在安装前，不能等第一次运行才询问。

解包需检查绝对路径、`..`、符号链接、大小写碰撞、膨胀大小及入口越界。声明的本地路径只在扩展包内解析；宿主不通过 shell 拼接用户参数。包摘要用于完整性与锁定版本，不能代替发布者可信身份。

按扩展版本与运行环境安装到用户目录，创建新环境并自检后切换；运行任务固定代码和环境版本，更新不得改它脚下的文件。资源/模型按需下载、校验并提供离线方式，不写 `/usr/share` 或 EXE 安装目录。

Windows 静态主程序可以运行外部扩展，但第三方 Python/模型仍需要独立环境。协议扩展不要求链接 LabelQt/Qt，但所分发解释器、依赖与模型的许可证和平台能力仍需单独说明。

### 11.2 权限不是沙箱

宿主能强制限制自己的快照、素材代理、密钥服务和提案提交；**不能靠 manifest 阻止同用户权限 Python 直接打开文件或自行联网**。

因此必须区分受信任代码扩展与只填写受限参数的声明式预设。要承诺“插件不能读其他文件”，需要额外操作系统沙箱设计与跨平台验证，不把这一承诺塞进首版文案。

密钥按安装身份、用户配置、contribution 和已批准端点绑定；端点更换或权限扩大需重新确认。禁止按扩展显示名称查任意 QtKeychain 记录，禁止新包冒用旧 ID 后自动获得密钥。日志默认不包含请求全文、响应全文、完整路径或环境；SDK 的脱敏只是防误泄露措施，不是对恶意代码的防护。

缓存有配额、清理和卸载选项。卸载/更新前停止 worker，清除或显式保留配置与任务草稿；已写入工程的正文/来源记录属于用户数据，不随插件卸载被删除。

## 12. 对现有代码的改造顺序

| 当前代码 | 推荐调整 | 必须保留 |
| --- | --- | --- |
| `AutomationService` / `AutomationRunner` | runner 已原生消费任务快照；后续 worker transport 独立，任务协调不塞回进程类 | 外部进程隔离、取消、诊断 |
| `AutomationManifestParser` | 只保留参数与密钥声明解析；任务契约由 AutomationProtocol 严格验证 | 明确的 v2 版本拒绝与清单错误 |
| `ProjectTaskService` / `AutomationProtocol` | 已替代旧 `AutomationOperationApplier`；继续扩展 profile 规划与审核，不恢复直接写入路径 | 宿主依赖、旧值、软删除和撤销语义 |
| `MainWindow` | 已删除自动化专属 mutation/undo 路径；后续仍只编排 UI | 与标签视图刷新、选择同步的既有边界 |
| 现有 Python SDK | v1 模块已删除；最小 v2 Task SDK 已提供，后续独立打包与完善开发工具链 | 原生稳定 ID 与任务范围，不维护旧 API |
| OCR/翻译官方脚本 | 旧脚本已删除；后续以独立提供者验证 profile，算法不控制宿主工作流 | 提供者契约测试与错误传播 |
| `ProjectTextSearchService` / 编辑层 | 提取确实共用的版本和变更验证，不让自动化依赖搜索 UI | 批量替换的一次撤销及旧值验证 |

先服务两个真实案例：一个无需依赖的文本转换、一个翻译提供者；再接 OCR。所有官方实现使用同一套契约测试，不用隐藏接口绕过审核，这样才能真正验证 SDK 是否好用。

分阶段交付：

1. **协议与提交最小闭环**：稳定身份、会话/版本、profile/提案规则、严格校验、原生批准策略与审核、共用编辑入口。旧协议拒绝路径单独测试，不恢复兼容转换。
2. **作者可开发**：最小 Python SDK、清单表单、脚手架、测试宿主、两个示例、本地安装/禁用/诊断。
3. **模型与长任务**：worker 生命周期、分项发布、取消、资源管理、OCR 素材和候选审核。
4. **持续会话与恢复**：新快照、提交事件订阅、阶段回执及受控工作流。复用第一阶段身份/版本；跨会话恢复继续核验 epoch/授权，不能仅按持久 ID 自动重放写入。

不列入首版：在线市场、任意自定义 Qt UI、通用工作流图编辑器、共享内存传图、跨扩展直接调用、公开 C++ ABI、自动执行任意 Agent 指令，以及未经沙箱验证的“不可信插件安全运行”。

## 13. 验收标准

“SDK 完成”至少要满足以下事实，而不是仓库里多一个 Python 文件：

- 新作者只依靠公开文档和模板，在不了解 C++ 内部类的情况下实现一个文本转换和一个翻译扩展。
- 两个独立翻译扩展自动进入同一翻译界面；切换后端不改变范围、审核、撤销与隐私规则。
- 不依赖 Qt 的测试宿主可以验证 API 正反例；有最小非 Python 协议示例证明没有把 Python 私有对象当传输格式。
- 正常、遗漏、重复、错误类型、未知字段、过期目标、emoji 定位、空文本与异常坐标有两端共享测试。
- 初始化失败、部分失败、取消/完成竞态、模型卡死、重复发布、磁盘满、日志污染 stdout 和插件更新中的任务均有明确行为。
- 修订模式、软删除、分组筛选、全文替换、页面管理、另存、合并、恢复与插件提案互不绕过各自边界。
- 应用与撤销/重做不重新调用算法、不额外计费、不误移焦点；无效结果不会清空正文或写错标签。
- 当前协议契约与旧协议拒绝测试进入 CI；Python SDK 示例对发布 wheel 测试，而不是只对源码目录 import 成功。
- 安装、发现和查看清单不会执行插件；安装依赖/实际运行前明确告知并获得代码信任。
- 文档分别有用户安装指南、作者快速入门、API 参考、协议规范、profile 示例、错误码、测试指南和迁移说明。

最终应做到：作者只写自己的算法和业务规则，用户只面对统一的漫画文字处理流程，而工程安全、审核、版本和撤销始终由 LabelQt 掌握。
