# 开发辅助工具

本项目提供 `scripts/devtool.py` 作为面向维护者和 Agent 的统一辅助入口。它不替代 CMake、CTest 或已有脚本，而是把常用检查和工程路由收束到一个稳定命令，减少每次开发前反复搜索文档和文件的成本。

## 常用命令

快速检查：

```bash
python scripts/devtool.py check --mode fast
```

这会复用 `scripts/check_fast.py`，检查翻译、官方脚本 Python 语法、Windows shared/static 发行树筛选与
许可证收集逻辑，以及 `git diff --check`。
翻译提取支持格式化后相邻的 C++ 字符串字面量，`test_translations.py` 覆盖连接、上下文和 Unicode 保真。

完整检查：

```bash
python scripts/devtool.py check --mode all --preset linux-debug
```

如果不传 `--preset`，工具会按当前平台选择默认 preset。Windows 上默认使用 VS preset。

只检查翻译：

```bash
python scripts/devtool.py check --mode i18n
```

## 模块路由

当不确定一个功能应该改哪些文件时，先用 `module-map`：

```bash
python scripts/devtool.py module-map proofread
python scripts/devtool.py module-map automation
python scripts/devtool.py module-map selection
```

输出会列出相关文件、文档和职责边界提示。这个命令尤其适合 Agent 在进入任务时快速定位入口，避免把逻辑顺手塞回 `MainWindow`。

不带参数会列出全部已知主题：

```bash
python scripts/devtool.py module-map
```

## 自动化脚本校验

校验官方脚本清单、入口文件和参数声明：

```bash
python scripts/devtool.py automation-validate
```

也可以显式传入脚本根目录：

```bash
python scripts/devtool.py automation-validate scripts/official scripts/custom
```

这个检查会验证：

- `script.json` 是否能解析。
- `apiVersion`、顶层 `name`、脚本 `id/name/entry` 是否存在。
- Python 入口文件是否存在并能通过语法编译。
- 参数类型是否属于当前支持范围。
- secret 参数是否声明了 `service`、`account` 和 `environment`。

## 发行树审计

Windows 便携包目录：

```bash
python scripts/devtool.py release-audit build/Release --kind windows-dir
```

Windows 静态包使用 `--kind windows-static-dir`，会额外检查静态链接 notice 和重链接文档。两种 Windows
审计都会要求 Qt 对应源码 notice、原生依赖许可证，并拒绝开发专用的 `scripts/official/test`。

Linux 安装根目录：

```bash
python scripts/devtool.py release-audit /tmp/labelqt-install --kind linux-install-root
```

这个命令只做轻量结构检查。正式发版仍应参考 [release-checklist.md](release-checklist.md)。

## 用户手册构建

用户手册位于 `manual/`，使用 Typst 编写，不参与主程序 CMake 构建。默认构建：

```bash
python scripts/build_manual.py
```

发布前使用真实截图并指定版本化输出名：

```bash
python scripts/build_manual.py --require-screenshots --output dist/LabelQt-v0.1.0-user-manual-zh_CN.pdf
```

如果本机没有 Typst，脚本会给出安装提示。截图素材放在 `manual/assets/`，准备步骤见
[截图清单](../manual/SCREENSHOTS.md)。`--list-missing-screenshots` 从 Typst metadata 收集引用并检查缺图；
`--with-screenshots` 使用已有图片、缺图占位，`--require-screenshots` 则要求素材齐全且缺图不覆盖原 PDF。
README 复用同目录中的三张概览图。合并工程保持纯文字，不要求 `merge-dialog.png`。

## Commit Message 初稿

根据当前 diff stat 输出仓库模板格式的初稿：

```bash
python scripts/devtool.py commit-message --title "fix(canvas): 修复 marker 拖动状态"
```

如果只想看 staged diff：

```bash
python scripts/devtool.py commit-message --staged
```

生成结果仍需要人工补充背景、风险和实际验证项。

## 使用原则

持久化安全约定见 [persistence-safety.md](persistence-safety.md)。四项工作流的实际接口、操作和限制见
[product-workflows.md](product-workflows.md)，原始设计另保留在 [product-design-proposal.md](product-design-proposal.md)。

- 新增可自动化的重复检查时，优先扩展 `devtool.py`，不要让每个 Agent 重新写一段临时脚本。
- `devtool.py` 应保持轻量、无第三方 Python 依赖，并尽量复用已有脚本。
- 对会修改工程内容的操作保持谨慎；当前工具以检查、路由和模板生成为主。
