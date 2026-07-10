#import "../style.typ": menu, note

= 快速开始

== Windows

Windows 正式发布包目前推荐使用静态单文件版本：

1. 下载 `LabelQt-v0.1.0-windows-x64-static.exe`。
2. 双击运行。
3. 通过#menu[文件 → 打开工程]选择 LabelPlus `.txt` 工程。

如果你要使用自动化脚本，需要另外在偏好设置里配置 Python。当前正式发布包默认不附带官方自动化脚本。

== Linux

Linux 正式发布包目前推荐 RPM：

```bash
sudo dnf install ./labelqt-0.1.0-1.fc44.x86_64.rpm
labelqt
```

RPM 使用系统 Qt 运行库，因此安装包体积会明显小于 Windows 静态版。

== 打开和保存工程

- 打开工程：#menu[文件 → 打开工程]。
- 新建工程：#menu[文件 → 新建工程]，选择图片所在目录。
- 保存工程：#menu[文件 → 保存]。
- 另存为：#menu[文件 → 另存为]。

无命令行参数启动时，LabelQt 会尝试恢复最近一次关闭时的工程和页面位置。

#note("保存习惯", [
  LabelQt 支持自动备份，但它不能替代正常保存。批量改动、校对或重命名前，建议先手动保存一次。
])
