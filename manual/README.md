# LabelQt 用户手册子工程

这里存放使用 Typst 编写的 LabelQt 用户手册。手册面向普通用户，不参与主程序 CMake 构建。

## 构建

仓库根目录执行：

```bash
python scripts/build_manual.py
```

也可以直接调用 Typst：

```bash
typst compile manual/main.typ dist/LabelQt-user-manual-zh_CN.pdf
```

手册默认使用思源宋体作为正文、思源黑体作为标题。Typst 识别的字体名通常为 `Source Han Serif` 和
`Source Han Sans`；如果系统未安装，会回退到 Noto CJK 或系统可用字体。

## 截图

手册源文件中保留了截图占位。准备发布 PDF 前，将实际截图放入 `manual/assets/`，并按
`manual/chapters/assets-needed.typ` 中的清单命名。

当前第一版即使没有截图也能正常编译，PDF 中会显示占位框。

当前手册截图来自 Fedora 44 KDE。其他平台或主题下，窗口装饰、控件外观和配色可能不同。

截图准备好后，可启用真实截图构建：

```bash
python scripts/build_manual.py --with-screenshots
```
