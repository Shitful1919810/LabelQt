# 发布检查清单

这份清单用于发布 LabelQt 的预览版或正式版。目标是让每次发布都能复用同一套步骤，减少遗漏运行时文件、许可证文件或测试验证的风险。

## 发布前确认

- 确认 `main` 分支干净，目标提交已经通过必要审查。
- 确认版本号、release tag 和 release 标题一致，例如：
  - tag：`v0.1.0-alpha.2`
  - 标题：`LabelQt v0.1.0-alpha.2`
- 更新并检查 release note。
- 确认 `README.md`、`THIRD_PARTY_NOTICES.md` 和 `docs/porting-dependencies.md` 没有与当前构建方式矛盾。
- 如需发布用户手册，确认 `manual/` 内容与当前功能一致，并准备好必要截图素材。

## 本地检查

推荐至少运行：

```bash
python scripts/check_fast.py
python scripts/check_all.py
```

如果修改了界面文本，还需要确认：

```bash
python scripts/check_translations.py
```

GUI 相关改动应做一次手动 smoke test：

- 启动程序。
- 打开 LabelPlus `.txt` 工程。
- 切页、缩放、拖动画面。
- 新增、编辑、删除、复制、剪切、粘贴标签。
- 保存工程。
- 打开偏好设置。
- 检查自动化脚本菜单能正常显示。

## 用户手册 PDF

用户手册由 `manual/` 下的 Typst 子工程生成，不参与主程序 CMake 构建。发布前可运行：

```bash
python scripts/build_manual.py --with-screenshots --output dist/LabelQt-v0.1.0-user-manual-zh_CN.pdf
```

如果 `manual/assets/` 中缺少截图，PDF 会显示占位框。正式发布前建议按 `manual/chapters/assets-needed.typ`
补齐主界面、页面管理和校对变更截图。合并工程章节使用纯文字说明，不需要截图。手册 PDF 作为独立 release 附件上传，不放进 Windows exe 或 Linux RPM。

## Windows 动态链接包

先构建并部署运行时：

```powershell
cmake --preset windows-vs-release
cmake --build --preset windows-vs-release
cmake --build --preset windows-vs-release --target deploy_windows
```

然后用打包脚本生成 zip。示例：

```bash
python scripts/package_windows_release.py \
  --source /path/to/deployed/Release \
  --version v0.1.0-alpha.2 \
  --variant shared \
  --dependency-license-dir /path/to/vcpkg/installed/x64-windows/share \
  --qt-version 6.x.y \
  --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0-alpha.2/qt-everywhere-src-6.x.y.tar.xz \
  --output-dir dist
```

Windows zip 应包含：

- `labelqt.exe`
- 运行所需 Qt DLL 和第三方 DLL
- Qt 插件目录，例如 `platforms`、`imageformats`、`styles`、`tls`
- `preference.json`
- `scripts/official`
- 空的 `scripts/custom` 目录
- `LICENSE.txt`
- `THIRD_PARTY_NOTICES.md`
- `QT_SOURCE_OFFER.txt`
- `licenses/` 下的第三方许可证文本

打包脚本会拒绝缺少 `qtbase` 或 `qtkeychain` 许可证目录的依赖清单。仍需人工核对所传目录来自本次构建
实际使用的 vcpkg triplet，并确认其中还包含所有随包分发的传递依赖许可证。

不应包含：

- `LabelQtTests.exe`
- `Qt6Test.dll`
- `.pdb`、`.ilk`、`.exp`、`.lib` 等开发产物
- CMake 构建缓存
- 用户本地脚本配置、API key、日志或模型缓存
- 开发专用的 `scripts/official/test`

打包后在一个干净目录里解压 zip，并从解压目录直接运行 `labelqt.exe`。不要只测试构建目录中的 exe。
解压后先运行 `python scripts/devtool.py release-audit <解压根目录> --kind windows-dir` 做结构审计。

可选：在 Windows 上安装 WiX Toolset v4+ 及其 UI 扩展后，可以基于同一个已部署的 Release 目录生成带标准安装向导的 MSI：

```powershell
python scripts/package_windows_msi.py `
  --source path\to\Release `
  --version v0.1.0-alpha.2 `
  --dependency-license-dir path\to\vcpkg\installed\x64-windows\share `
  --qt-version 6.x.y `
  --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0-alpha.2/qt-everywhere-src-6.x.y.tar.xz `
  --output-dir dist
```

MSI 会显示“下一步、安装路径、完成”等标准界面，把程序安装到 `Program Files\LabelQt`，并携带与 zip 相同的运行时文件、默认 `preference.json` 和官方脚本。发布 MSI 前应额外在干净 Windows 环境中测试安装、启动、卸载和升级流程。
打包脚本会把 `alpha.N`、`beta.N`、`rc.N` 和正式版映射到单调递增的 Windows Installer 数值版本，避免
同一 `0.1.0` 系列预发行包被识别为相同版本。

## Windows 静态链接包

使用静态 Qt、静态 QtKeychain 和计划随包使用的其他静态依赖配置独立构建目录：

```powershell
cmake --preset windows-vs-static-release
cmake --build --preset windows-vs-static-release
```

Windows 静态版可以使用同一个打包脚本的静态变体生成完整 ZIP。示例中的 Qt 源码 URL 必须指向由
LabelQt 项目控制、与本次构建完全对应的源码归档：

```powershell
python scripts/package_windows_release.py `
  --source path\to\static\Release `
  --version v0.1.0-alpha.2 `
  --variant static `
  --dependency-license-dir path\to\vcpkg\installed\x64-windows-static\share `
  --qt-version 6.x.y `
  --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0-alpha.2/qt-everywhere-src-6.x.y.tar.xz `
  --relinking-instructions-url https://github.com/Shitful1919810/LabelQt/blob/v0.1.0-alpha.2/docs/windows-static-relinking.md `
  --output-dir dist
```

打包器会把 `labelqt-static.exe` 规范化为归档内的 `labelqt.exe`，写入
`STATIC_LINKING_NOTICE.txt` 和 `RELINKING.md`，并拒绝缺少静态合规参数的打包请求。静态发布检查还包括：

- 使用依赖检查工具确认没有意外的 Qt、QtKeychain 或 MSVC 运行库 DLL 依赖。
- 验证 Windows 平台插件、PNG/JPEG/SVG、系统密钥环、主题、翻译和官方自动化脚本。
- 附带实际静态链接版本对应的 Qt、QtKeychain 及其他依赖许可证。
- 提供项目自身控制的 Qt 对应源码获取方式，并记录精确版本、构建参数、补丁和重链接步骤。
- 实际修改一次 Qt 源码，按 `RELINKING.md` 从头构建并运行修改后的程序，验证重链接材料不是仅在纸面上成立。
- 在未安装 Qt、vcpkg 和 Visual Studio 的干净 Windows 环境中完成启动及核心功能测试。
- 项目许可证和第三方声明必须保留。自动化脚本是否随包发布由版本策略决定；不发布脚本时，Windows
  打包命令传入 `--exclude-automation-scripts`，Linux 配置传入
  `-DLABELQT_INSTALL_OFFICIAL_AUTOMATION_SCRIPTS=OFF`，并确认发行树中不存在整个 `scripts` 目录。
- 解压后运行 `python scripts/devtool.py release-audit <解压根目录> --kind windows-static-dir`。

若某个版本改为直接发布单个静态 exe，则必须同时上传单独的静态合规材料包。材料包至少包含实际链接依赖的
许可证、Qt 对应模块源码、构建所用 vcpkg commit/port 补丁与 triplet、构建信息、项目许可证、第三方声明和
重链接说明。单 exe 不是省略 LGPL 合规材料的理由。

## Linux RPM

Linux release 构建：

```bash
cmake --preset linux-release -DLABELQT_PRERELEASE=alpha.2 -DLABELQT_RPM_DIST_TAG=fc44
cmake --build --preset linux-release
```

生成正式 RPM：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G RPM
```

RPM 必须在目标 Fedora/openSUSE 系构建环境中生成，并检查依赖、包内容和安装/卸载行为。仓库保留以下
DEB 命令用于实验和社区验证，但 DEB 不上传为正式 release 产物：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G DEB
```

安装包应遵循 Linux 文件布局：

- 可执行文件：`/usr/bin/labelqt`
- 文档和许可证：`/usr/share/doc/labelqt`
- 第三方许可证文本：`/usr/share/doc/labelqt/licenses`
- 桌面入口：`/usr/share/applications/org.labelqt.LabelQt.desktop`
- 图标：`/usr/share/icons/hicolor/scalable/apps/org.labelqt.LabelQt.svg`

Linux RPM 不捆绑默认 `preference.json`、Python、OCR 模型或自动化脚本依赖。默认构建会把官方脚本安装到
`/usr/share/labelqt/scripts/official`，也可以通过发行选项完全排除。偏好设置默认值由程序内置提供；用户仍可
通过自动化菜单打开用户脚本目录并自行安装脚本。Windows 便携包同样可以选择携带或完全排除 `scripts`。

RPM 元数据应显示 `Shitful <shitful233@gmail.com>`、项目主页、完整描述和正确的预发行 Release 字段。
正式包不应包含 `scripts/official/test`。运行 `rpm -qpi`、`rpm -qpR`、`rpm -qpl` 和 `rpmlint` 检查后，
再对 RPM 及其 CPack 生成的 SHA-256 文件签名或发布校验信息。

Linux 静态构建和 DEB 包均为实验性产物，不进入正式发布附件，也不应在 release note 中标记为受支持平台包。

## 发布到 GitHub

- 在对应提交上创建 tag。
- 按该版本发布矩阵上传 Windows 动态 ZIP（如提供）、Windows 静态 ZIP 或“静态 exe + 合规材料包”，以及
  Linux RPM；如已生成用户手册 PDF，也作为独立附件上传。DEB 与 Linux 静态实验产物不上传为受支持附件。
- 粘贴 release note。
- 标记 alpha/beta/pre-release 状态。
- 发布后下载 GitHub 页面上的附件，再做一次解压运行检查。
