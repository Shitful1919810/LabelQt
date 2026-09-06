# LabelQt 自动化 v2 开发指南

## 当前边界

本分支仅支持 v2：没有 v1 SDK、`operations`、页名/行号转换器或兼容入口。
脚本运行于独立 Python 进程；读取按范围裁剪的值快照，返回一个原子变更批次。
不提供实时工程对象，不允许脚本通过 API 保存 TXT、重命名图片或修改任意元数据。

当前是一次性文件传输，不是常驻 worker/双向 IPC。运行期间界面仍保护工程编辑；
宿主底座已具备字段级冲突判断，但这不意味着按页并行 UI 已开放。
完整身份、版本和线程规则见 [基础契约](automation-foundation.md)。

## 安装与示例

从“自动化 > 打开用户脚本目录”进入，给每个扩展建立独立子目录，
放入 `script.json` 和 Python 入口。可附带 `requirements.txt`。
官方脚本优先从 exe 旁的 `scripts/official` 读取，否则从安装数据目录读取；
用户目录和便携目录的选择规则见 `core/RuntimePaths`。

SDK 为 [labelqt_task.py](../scripts/official/sdk/labelqt_task.py)，仅依赖 Python 3.10+ 标准库。
自定义扩展可将该文件与其 MIT 许可证一起放在入口旁边，然后 `from labelqt_task import Task`。
无需 Qt、PySide、PyQt 或模型库。当前尚未提供 PyPI/wheel 发布和脚手架。

开发构建的 `Test` 子菜单包含三个 v2 示例（发行打包仍排除该目录）：

- `word_count.py`：只读整个工程的文本。
- `prefix_selected.py`：只修改运行时所选 Label 的文本。
- `add_label.py`：在当前页新增 Label，由宿主分配新 ID。

旧 OCR/AI 翻译脚本、配置脚本和旧 SDK 已移除，不再宣称这些具体后端开箱即用。
不删除用户已保存的密钥、配置或自定义脚本；旧脚本清单会被跳过并报告版本错误。

## 清单与授权范围

```json
{
  "apiVersion": 2,
  "name": "Text Tools",
  "scripts": [{
    "id": "prefix",
    "name": "Prefix Selected Labels",
    "entry": "prefix.py",
    "task": {
      "scope": "selectedLabels",
      "read": ["text"],
      "write": ["text"]
    },
    "parameters": [{
      "key": "prefix", "label": "Prefix", "type": "text", "default": "[Review] "
    }]
  }]
}
```

清单也可直接在顶层声明单个入口及 `task`。
`apiVersion` 必须是数字 2；旧清单不自动升级。

| task 字段 | 语义 |
| --- | --- |
| scope（必填） | `project`、`currentPage`、`selectedLabels` |
| read（必填数组） | `text`、`position`、`group`、`deleted`、`imageSource` |
| write（必填数组） | 前四种字段；空数组表示不允许修改已有 Label |
| allowCreate | 默认 false；独立授权在捕获的页面新增 Label，不授权修改已有 Label |
| pageOrder | 默认 false；读取页序并加入页序依赖 |
| groups | 默认 false；请求分组 ID/名称目录 |
| metadata | 默认 false；请求只读工程注释 |

`selectedLabels` 没有选中时拒绝启动，**不会回退为整页或全工程**，也不能申请新增。
范围固定在运行前，不随之后的浏览选择变化。未知 task 字段、重复/未知权限和错误类型会被拒绝。

当前范围授权方式是用户主动运行受信任的本机脚本，宿主根据清单和当前选择构造权限；
尚无插件安装权限确认、独立参考范围选择、候选审核或沙箱。脚本不能通过返回值扩大权限。
只读上下文与写目标分离的 C++ 服务能力见基础契约，清单首版只提供以上三个范围。

## SDK 用法

```python
from labelqt_task import Task

task = Task.from_cli()
for label in task.active_labels:
    task.update(label, text=task.parameters["prefix"] + label.text)
task.finish(summary="Text changes proposed", quiet=True)
```

- `task.pages` / `task.labels` 是不可变快照值；未读取字段为 None，而不是伪造空文本或零坐标。
- `task.active_labels` 排除软删除标签。
- `task.groups` 是分组 UUID 到名称的只读映射；`task.parameters`、`selection` 也只读。
- `task.update(label, text=..., position=(x,y), group_id=..., deleted=True)` 累积字段变更。
  同一 Label 的多次调用合并为一项，后写字段覆盖此前提案；不修改输入快照。
- `task.create(page, text=..., group_id=..., position=(x,y))` 提议新增，不生成假 Label ID。
- `task.finish(...)` 原子写出一次提案；不代表工程已经提交或保存。
  错误应抛异常/非零退出，不能用成功报告掩盖失败。

文本原样保留，包括真正的空字符串。位置为有限的 [0,1] 归一化坐标。
未知字段、错误类型、重复结果项、越权和陈旧任务会在宿主侧再次验证，不能依赖 SDK 自律。

## 线上格式

调用：`python entry.py --input input.json --output output.json`。
stdout/stderr 是日志，不能包含密钥。输入顶层包括：

- `apiVersion: 2`、`taskId`、`sessionId`、`projectId`；
- `revision`：十进制字符串，避免 JSON 浮点数损失 64 位精度；
- `pages: [{id, imageSource?: {name, path}}]`；
- `labels: [{id, pageId, deleted, text?, position?: {x,y}, groupId?}]`；
- `groups`：UUID 到名称的对象；`metadata`：注释数组；
- `parameters` 与 `selection`；无图像选区时 selection 为 {}，
  否则为 `{pageId,x,y,width,height}`，仅在选区页包含于快照时提供。

字段读取与写依赖都由宿主记录。文本任务不会得到全部图片路径；
`imageSource` 会产生文件哈希 IO，且不保证图片文件不可被其他进程修改。
只有声明 `pageOrder` 时才保证页面按工程顺序排列。

输出最小示例（UUID 应复制实际输入中的值）：

```json
{
  "apiVersion": 2,
  "taskId": "11111111-1111-4111-8111-111111111111",
  "sessionId": "22222222-2222-4222-8222-222222222222",
  "changes": [{
    "pageId": "33333333-3333-4333-8333-333333333333",
    "labelId": "44444444-4444-4444-8444-444444444444",
    "text": "New text"
  }],
  "summary": "One change proposed",
  "quiet": true
}
```

UUID 使用小写、无花括号的规范字符串；不得为空 UUID。
已有 Label 必须提供 `pageId + labelId`；新增必须**省略** labelId 并提供 text/groupId/position。
省略的字段不修改，不能用 null 代表“忽略”。
同一 Label 只允许一项结果，可同时修改多个字段。`deleted: true` 表示软删除。
可选展示字段为 `summary`、`title`、`text`、`quiet`；quiet 只影响成功提示，不影响校验或权限。

结果必须回显当前 taskId/sessionId，不能回显任意任务凭证。清单/输出读取上限 16 MiB，
一次最多 10,000 项变更与 4 Mi 个 UTF-16 文本单元。提交全部通过后只形成一条普通撤销记录。
取消、失败、超时、错误身份或冲突不应用任何变更。

## 参数和密钥

保留现有原生参数表单：text、textarea/multiline、group、choice/select/enum、
boolean/bool、file、directory、secret。group 参数返回名称，修改时应从快照 groups 查到对应 UUID。

secret 参数包含 `key/service/account/environment`（可另设 secretKey），
值经 QtKeychain 保存，不写入 input.json。需要使用的入口在 `secrets` 数组中声明
`key/service/account/environment/required`，宿主仅注入对应子进程环境。
普通 `environment` 只能放非敏感选项。脚本运行日志、JSON、工程、配置都不能存密钥。

独立进程是故障隔离，不是操作系统安全沙箱；只运行可信脚本。
SDK 不提供写 TXT/图片的接口，但无法阻止用户安装的任意 Python 代码绕开接口访问磁盘和网络。

## 验证

```bash
python scripts/test_automation_sdk.py
python scripts/devtool.py automation-validate
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

`AutomationProtocolTests` 覆盖严格协议、范围和真实 Python SDK → C++ 提交 → 撤销/重做；
`AutomationTaskTests` 覆盖并发依赖、身份和会话失效。未来的持续通信/profile 设计仍见
[API 边界](automation-api-boundaries.md)，其示例不是本版本已经提供的 SDK 接口。
