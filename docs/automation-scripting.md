# LabelQt 自动化脚本开发指南

LabelQt 的自动化脚本采用“外部 Python 进程 + JSON 文件交换”的模式。脚本不嵌入主程序，也不直接修改 `.txt` 工程文件；它读取主程序导出的 `input.json`，再写回 `output.json`。如果需要修改工程，脚本输出结构化 `operations`，由 C++ 侧校验、应用并注册撤销/重做。

## 目录结构

发行包中的脚本目录位于可执行文件旁边：

```text
scripts/
  official/      官方脚本和示例
  custom/        用户脚本
```

每个脚本组一个目录。目录里至少包含 `script.json` 和入口 Python 文件，也可以包含 `requirements.txt` 和资源文件等。发行版中的脚本目录可能是只读的；需要持久化本机配置时，优先使用 SDK 提供的用户配置目录辅助函数。

```text
scripts/official/my_tool/
  script.json
  run.py
  requirements.txt
```

`script.json` 可以定义单个脚本，也可以用顶层 `scripts` 数组定义多个菜单项。多脚本目录会在“自动化”菜单里显示为一个子菜单，子菜单顺序保持 `script.json` 中的声明顺序，因此建议把 `Configuration` 类脚本放在第一个。

## 最小 SDK

官方提供一个轻量辅助库：

```text
scripts/official/sdk/labelqt_automation.py
```

它不是独立 Python 包，不需要安装。官方示例脚本通过相对路径导入：

```python
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext
```

脚本通常先创建 `AutomationContext`，再通过属性和方法读取工程快照、构造操作并写出结果：

```python
ctx = AutomationContext.from_file(args.input)

operations = []
for label in ctx.selected_labels:
    operations.append(label.delete())

ctx.write_output(args.output, "Delete Labels", "Done.", operations)
```

这个库封装最常用的无依赖操作，例如读取输入、写结果、读取当前页、读取选中标签、读取参数、通过 `Page` / `Label` 对象访问工程数据、按一基页码区间读取页面或标签、构造常见操作、读写脚本本机配置等。它刻意保持很小，方便用户复制和理解。

脚本需要保存非敏感配置时，建议使用：

```python
from labelqt_automation import load_script_config, save_script_config

config = load_script_config(__file__)
config["example"] = "value"
save_script_config(__file__, config)
```

SDK 会把配置写入当前用户的 LabelQt 配置目录，例如 Linux 的 `~/.config/LabelQt/automation/...`、Windows 的 `%APPDATA%/LabelQt/automation/...`。为兼容早期版本，`load_script_config()` 仍会在用户配置不存在时读取脚本目录下的旧 `config.json`。

## 脚本调用方式

主程序运行脚本时会传入两个参数：

```bash
python run.py --input /tmp/input.json --output /tmp/output.json
```

脚本必须读取 `--input`，并在成功时写出 `--output`。脚本可以向 stdout/stderr 输出日志；用户在偏好设置里打开自动化日志窗口后，可以实时看到这些输出。

## input.json

`input.json` 是一次运行开始时的工程快照，默认只导出未删除标签。主要字段如下：

```json
{
  "apiVersion": 1,
  "scope": "project",
  "options": {
    "includeDeletedLabels": false
  },
  "parameters": {},
  "selection": {
    "hasSelection": true,
    "imagePath": "/path/to/001.png",
    "rect": {
      "left": 0.1,
      "top": 0.2,
      "right": 0.5,
      "bottom": 0.4,
      "width": 0.4,
      "height": 0.2
    }
  },
  "context": {
    "currentPage": {
      "hasPage": true,
      "index": 0,
      "number": 1,
      "name": "001.png",
      "imagePath": "/path/to/001.png"
    },
    "selectedLabels": []
  },
  "project": {
    "path": "/path/to/project.txt",
    "sourceName": "project.txt",
    "currentPage": "001.png",
    "groups": ["框内", "框外"],
    "imagePaths": ["/path/to/001.png"],
    "pages": [
      {
        "index": 0,
        "name": "001.png",
        "imagePath": "/path/to/001.png",
        "labels": [
          {
            "labelIndex": 0,
            "visibleIndex": 1,
            "group": "框内",
            "text": "原文",
            "x": 0.3,
            "y": 0.4
          }
        ]
      }
    ]
  }
}
```

`labelIndex` 是工程内部标签下标，供 `operations` 精确引用；`visibleIndex` 是 UI 展示序号，只用于展示。脚本不要把 `visibleIndex` 当成修改目标。

`parameters` 来自 `script.json` 声明的运行前参数窗口。`secret` 参数不会出现在 `input.json`；密钥由主程序保存到系统密钥环，并按脚本声明注入到子进程环境变量。

`project.path` 是当前 LabelPlus 工程文件路径。`project.currentPage` 是当前页图片名的便捷字段；更完整的当前 UI 状态应读取 `context.currentPage`。如果脚本需要工程目录，建议从 `project.path` 自行取父目录，而不是依赖额外字段。

## output.json

最小成功输出如下：

```json
{
  "apiVersion": 1,
  "summary": "处理完成",
  "result": {
    "type": "message",
    "title": "脚本结果",
    "text": "处理完成。"
  },
  "quiet": false
}
```

如果顶层 `"quiet": true`，脚本成功后不会弹出结果窗口，只会更新状态栏。失败仍然会提示用户。

## 操作列表

脚本需要修改工程时，输出 `operations`。主程序会统一校验、应用并纳入撤销/重做。

当前支持的操作：

```json
{
  "type": "setLabelText",
  "page": "001.png",
  "labelIndex": 0,
  "text": "译文"
}
```

```json
{
  "type": "setLabelGroup",
  "page": "001.png",
  "labelIndex": 0,
  "group": "框外"
}
```

```json
{
  "type": "setLabelPosition",
  "page": "001.png",
  "labelIndex": 0,
  "x": 0.35,
  "y": 0.45
}
```

```json
{
  "type": "deleteLabel",
  "page": "001.png",
  "labelIndex": 0
}
```

```json
{
  "type": "addLabel",
  "page": "001.png",
  "group": "框内",
  "text": "OCR 结果",
  "x": 0.5,
  "y": 0.5
}
```

坐标是图像归一化坐标，范围为 `0.0` 到 `1.0`。脚本应主动 clamp 坐标，避免输出无效值。

## script.json 参数

支持的参数类型包括：

- `text`：单行文本。
- `textarea` / `multiline`：多行文本，适合 prompt。
- `group`：当前工程分组。
- `choice` / `select` / `enum`：固定选项。
- `boolean` / `bool`：复选框。
- `file`：文件路径。
- `directory`：目录路径。
- `secret`：密钥输入框，保存到系统密钥环。

密钥参数示例：

```json
{
  "key": "deepseekApiKey",
  "label": "DeepSeek API key",
  "type": "secret",
  "secretKey": "deepseekApiKey",
  "service": "LabelQt",
  "account": "deepseek_api_key",
  "environment": "DEEPSEEK_API_KEY"
}
```

真正需要读取密钥的运行脚本还应声明 `secrets`，主程序会在运行前读取系统密钥环并注入环境变量。脚本只读取环境变量，不要把 API key 写入 `config.json`、日志或结果 JSON。

## 示例职责

`scripts/official/test` 既用于测试自动化框架，也用于展示推荐写法：

- 用 `AutomationContext.from_file()` 读 `input.json`，用 `ctx.write_output()` 写 `output.json`。
- 用 `ctx.parameters` 接收用户参数。
- 用 `ctx.ops` 构造 `operations` 修改工程，而不是直接改 `.txt`。
- 对没有当前页、没有选中标签、参数为空等情况给出温和结果。
- 让有副作用的脚本输出可撤销操作。

`scripts/official/ocr_preview` 展示如何读取当前选区、当前页图像路径、OCR 结果并输出 `addLabel`。`scripts/official/ai_translate` 展示如何使用系统密钥环注入的 API key，以及如何实现预览、应用翻译和跨页应用翻译；跨页应用会把整个页码区间作为上下文一次性提交给模型，并用返回的 `page + labelIndex` 定位修改目标。

## 建议

- 脚本应当把工程快照视为只读。
- 长任务要定期输出进度日志，方便用户判断是否卡住。
- 不要输出无限日志；主程序会截断过大的日志内容。
- 不要假设标签分组一定存在；如果要写入某个分组，应让用户在参数窗口选择或在文档里明确要求。
- 不要把密钥、私有路径、模型目录提交到仓库。脚本本地配置建议通过 `save_script_config()` 写入用户配置目录，不要依赖发行包脚本目录可写。
- 如果脚本损坏、入口文件不存在或脚本清单格式错误，主程序会跳过该脚本并在状态栏显示警告；脚本作者仍应尽量让错误信息清晰可读。
