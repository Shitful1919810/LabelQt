# 依赖与移植说明

这份文档记录 LabelQt 当前需要的构建依赖、运行依赖、可选增强项和发布时需要注意的边界。它不是许可证全文，发布前仍应以各依赖上游许可证为准。

## Android 技术预览

Android 构建说明见 [android-build.md](android-build.md)，完整目标见 [实施方案](android-port-plan.md)。
已按目标拆分 Widgets 与 Qml/Quick/QuickControls2，Android 不启用 Python runner 和其专属密钥依赖，
但保留共用身份、版本和提交机制。系统输入法不需要额外引入 Qt Virtual Keyboard。

QQ 收发使用 Android 标准 Intent、文档选择器和受限 FileProvider，不计划引入 QQ SDK。
移动目标 ZIP 读写使用 libarchive：Android 固定 3.8.11 源码与 SHA256，仅启用 zlib 压缩依赖，
关闭命令行工具、XML、密码学和额外压缩依赖；主机移动测试使用系统 libarchive >= 3.6。
具体运行时只注册 ZIP 读取器，拒绝加密/分卷/ZIP64、链接、路径逃逸和资源预算超限。
Android 用 NDK 自带 zlib，不能误链接主机 `/usr/lib`；不引入 Qt Virtual Keyboard 或 QQ SDK。
工具链为 Qt 6.10.2、NDK r27c、SDK 36、JDK 17、Qt 模板的 Gradle 8.14.3/AGP 8.10.1，最低 API 28。
真机与 16 KiB 验收仍是正式发布门槛。

## C++ 构建要求

必需：

- CMake 3.24 或更新版本。
- 支持 C++23 的编译器。
- Qt 6.5 或更新版本：
  - `Core`
  - `Gui`
  - `Widgets`
  - `Concurrent`
- QtKeychain。
- zlib 开发包（`ZLIB::ZLIB`）：工程元数据使用其流式解压 API 限制实际输出大小，而不是直接使用
  无输出上限的 `qUncompress()`。兼容 zlib ABI 的 zlib-ng 也可使用。
  Fedora 安装 `zlib-ng-compat-devel`，Debian/Ubuntu 安装 `zlib1g-dev`；Windows 使用与 Qt 相同
  vcpkg triplet 的 `zlib`。静态发行必须使用静态 zlib，并在依赖合规材料中包含其版本和许可证。

常见平台工具链：

- Linux：GCC 或 Clang，Ninja 或 Makefiles，发行版提供的 Qt 6 开发包。
- Windows：Visual Studio 2022 Build Tools 或完整 Visual Studio，Qt 6 MSVC 包，Ninja 或 Visual Studio CMake generator。
- macOS：Xcode Command Line Tools，Qt 6 for macOS，Ninja 或 Xcode generator。

项目提供 CMake presets，优先用 preset 构建：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

如果本机 ccache 干扰 CMake 检测 `Threads`，可以按 `AGENTS.md` / `CONTRIBUTING.md` 中的方式临时设置 `CCACHE_DISABLE=1`。

## C++ 可选依赖

- Qt Svg：可用时会链接，用于更完整地显示内置 Breeze 主题中的 SVG 图标。项目应能在没有 Qt Svg 时构建。
- Qt LinguistTools：构建翻译资源时使用。`.ts` 文件在 `translations/`，`.qm` 会通过 Qt 资源编进程序。

日常 debug 构建仍允许缺少可选增强依赖；正式 Windows 与 Linux release presets 会强制要求 Qt Svg、
Qt LinguistTools，避免同一版本因构建机器差异而缺少主题图标或翻译。
- Qt Test：测试目标使用，普通运行不需要。

Qt Concurrent 目前用于把外部工程对比中的页面匹配和 diff 计算放到后台线程，避免大图工程比较时主界面无响应。图片加载缓存仍使用项目自己的 `ImagePageCache`。

## 自动化脚本运行时

主程序不嵌入 Python。自动化脚本通过外部 Python 进程运行：

```bash
python script.py --input input.json --output output.json
```

因此：

- 不使用自动化脚本时，LabelQt 主程序本体不需要 Python。
- 使用自动化脚本时，SDK 需要可用的 Python 3.10+。
- Python 命令、解释器参数、是否自动安装 `requirements.txt`、pip 镜像源都在偏好设置中配置。
- 自动安装依赖会复用自动化日志窗口，并支持用户取消。

当前 `scripts/official/sdk` 与 `scripts/official/test` 仅依赖 Python 标准库。
旧 OCR/AI 翻译及其配置脚本已删除，不再提供或要求 Pillow/PaddleOCR/manga-ocr/Transformers。
未来模型/网络后端应使用 v2 API，并分别声明依赖和许可证；不随 SDK 隐式安装模型栈。

## 运行时文件布局

构建后，程序会优先查找可执行文件目录旁边的 `preference.json` 和 `scripts` 目录，方便开发构建和 Windows
便携包直接携带示例配置与官方脚本。Linux 安装包会把官方脚本安装到 `/usr/share/labelqt/scripts/official`，用户自定义脚本仍放在用户脚本目录。`preference.json` 的默认值由
`AppPreferences` 内置提供；用户保存偏好设置时，如果 exe 旁边已有配置文件，会继续写回该文件，否则写入用户配置目录。用户脚本目录可以通过程序的“自动化 > 打开用户脚本目录”打开。

翻译文件不需要额外复制：构建时 `qt_add_translations()` 会把 `labelqt_*.qm` 编进资源路径 `/i18n`。`TranslationManager` 仍支持从可执行文件旁边的 `i18n` 目录加载翻译，方便以后做外部语言包。

## Windows 发布注意事项

动态 Qt 构建需要部署 Qt 运行时。项目提供 `deploy_windows` 目标，会调用 `windeployqt` 把 Qt DLL、平台插件和相关资源复制到 exe 旁边：

```powershell
cmake --build --preset windows-vs-release --target deploy_windows
```

如果使用 vcpkg 或自建 Qt，需确认 CMake 找到的是期望的 Qt 包。动态 Qt 会生成依赖 Qt DLL 的 exe，这是正常情况。

制作 Windows 预编译发布包时，应以 `deploy_windows` 生成后的 Release 目录为基础：

- 保留 `labelqt.exe`、运行所需 DLL 和 Qt 插件目录。
- 删除 `LabelQtTests.exe` 与 `Qt6Test.dll`，它们只属于测试目标。
- 附带仓库根目录的 `LICENSE.txt` 和 `THIRD_PARTY_NOTICES.md`。
- 不要把 Qt SDK、Qt 源码、构建缓存、`compile_commands.json`、用户脚本配置或 API key 打进发布包。Windows 便携包可以携带根目录 `preference.json` 和 `scripts/official`，用于提供默认体验和官方脚本。

推荐使用统一打包脚本：

```bash
python scripts/package_windows_release.py --source /path/to/deployed/Release --version v0.1.0-alpha.2 \
  --variant shared --dependency-license-dir /path/to/vcpkg/installed/x64-windows/share \
  --qt-version 6.x.y --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0-alpha.2/qt-everywhere-src-6.x.y.tar.xz \
  --output-dir dist
```

仓库还提供 `windows-vs-static-release` 静态 preset。它在独立构建目录中复用 `LabelQt` 应用目标，避免
静态版与动态版维护两份源文件和依赖配置。Windows 静态版计划作为正式发行产物：

- 必须使用真正静态构建的 Qt。
- QtKeychain 等依赖也需要有静态库，否则仍可能出现额外 DLL。
- 每次发布前必须完成 Qt 与静态依赖的许可证、对应源码、重链接说明和干净环境验证。
- 静态 ZIP 打包器会强制要求原生依赖许可证、Qt 精确版本、项目控制的对应源码地址和重链接说明地址。若直接
  发布单 exe，则必须把同样的信息连同实际 Qt 源码和 vcpkg 补丁放入独立合规材料包，随同 exe 发布。

完整重链接流程见 `docs/windows-static-relinking.md`。动态 ZIP 和 MSI 同样要求传入对应 vcpkg triplet 的
`share` 目录、Qt 精确版本和项目控制的对应源码 URL，确保随包 DLL 的许可证与源码义务不会遗漏。

## Linux RPM 与实验性 DEB 打包

普通 Linux release 构建生成的是动态链接可执行文件。正式 Linux 二进制发布物为 RPM，应在目标
Fedora/openSUSE 系环境中生成：

```bash
cmake --preset linux-release -DLABELQT_PRERELEASE=alpha.4 -DLABELQT_RPM_DIST_TAG=fc44
cmake --build --preset linux-release
cpack --config build/linux/release/CPackConfig.cmake -G RPM
```

`LABELQT_PRERELEASE` 支持 `alpha.N`、`beta.N` 和 `rc.N`，正式版留空；`LABELQT_RPM_DIST_TAG` 应对应实际
构建和验证环境，不要把在较新 Fedora 上构建的 RPM 标成通用 Linux 包。

仓库保留 DEB 生成器用于实验和社区验证，但它不属于受支持的正式发布矩阵。DEB 必须在 Debian/Ubuntu
环境中生成，不能使用 Fedora 上缺少 Debian 包数据库的构建结果作为发行物。默认 `package` 目标只生成
RPM；配置时开启 `-DLABELQT_ENABLE_EXPERIMENTAL_DEB_PACKAGE=ON` 才会把 DEB 加入默认生成器，也可以单独运行：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G DEB
```

打包工具要求：

- 实验性 `.deb`：通常需要 `dpkg-dev`、`fakeroot`，并依赖 `dpkg-shlibdeps` 自动分析动态库依赖。
- 正式 `.rpm`：通常需要 `rpm-build`，发布前应检查自动生成的 SONAME 依赖并在目标系统安装测试。

安装包内容：

- `/usr/bin/labelqt`
- `/usr/share/applications/org.labelqt.LabelQt.desktop`
- `/usr/share/icons/hicolor/scalable/apps/org.labelqt.LabelQt.svg`
- `/usr/share/doc/labelqt/LICENSE.txt`
- `/usr/share/doc/labelqt/THIRD_PARTY_NOTICES.md`
- `/usr/share/doc/labelqt/licenses/` 下的内置第三方资源许可证

RPM 和实验性 DEB 不会把默认 `preference.json`、Python 解释器、OCR 模型或自动化脚本依赖打包进去。
`LABELQT_INSTALL_OFFICIAL_AUTOMATION_SCRIPTS` 决定是否把官方脚本作为只读资源安装到
`/usr/share/labelqt/scripts/official`；关闭时原生包不包含任何自动化脚本。自动化脚本依赖仍由用户按脚本目录
中的 `requirements.txt` 安装。Linux 原生包也不会捆绑整套 Qt 运行库，实际依赖由目标发行版的包管理器解析和安装。
开发用 `scripts/official/test` 在任何发行模式下都不应进入正式 RPM 或实验性 DEB。

Linux 静态构建目前不在正式发布矩阵中。若后续加入，仅作为实验性便携构建；它不应替代动态链接 RPM，
也不能在未验证 glibc、XCB/Wayland、OpenGL、QtKeychain 和静态插件依赖的情况下宣称为完全静态或跨发行版通用。

## 国际化工作流

项目采用 Qt Linguist：

1. C++ 用户可见文本使用 `tr()`。
2. 更新四份 `.ts`：
   - `translations/labelqt_zh_CN.ts`
   - `translations/labelqt_zh_TW.ts`
   - `translations/labelqt_ja_JP.ts`
   - `translations/labelqt_en_US.ts`
3. 运行：

```bash
python scripts/check_translations.py
cmake --build --preset linux-debug --target release_translations
```

语言列表由 `TranslationManager` 从 `:/i18n` 和可执行文件旁的 `i18n` 目录动态发现。偏好设置中的语言变更目前需要重启后生效。

## 依赖选择原则

优先使用 Qt 已经提供的能力：

- JSON：`QJsonDocument`、`QJsonObject`、`QJsonArray`。
- 设置：`QSettings`。
- 进程：`QProcess`。
- 图片：`QImage`、`QPixmap`、`QGraphicsView`、`QGraphicsScene`。
- 翻译：`QTranslator`、Qt Linguist。
- 撤销/重做：`QUndoStack`、`QUndoCommand`。

只在 Qt 不擅长或已有专门库明显更合适时添加依赖：

- 密钥存储：QtKeychain。
- OCR/AI：外部 Python 脚本和 JSON 桥接，不嵌入到 C++ 主程序。

新增依赖前必须检查：

- 是否跨平台。
- 是否与 MIT 项目和 Qt LGPL 动态链接发布策略兼容。
- 是否需要在 `THIRD_PARTY_NOTICES.md`、README 或发行包中附带第三方声明。
- 是否会把 GPL-only 义务传导给整个发布包。

## 许可与致谢边界

LabelQt 源码使用仓库根目录 `LICENSE.txt` 的 MIT 许可证。第三方内容分三类：

- 链接或运行时依赖：Qt、QtKeychain 等，发布二进制时需要带对应第三方声明。
- 仓库内置资源：BreezeStyleSheets 位于 `resources/themes/breeze`，本地保留 MIT 和 Apache 2.0 声明。
- 用户运行时安装/调用的脚本依赖和服务：PaddleOCR、manga-ocr、Pillow、Transformers、DeepSeek API 等，不随 C++ 主程序一起编译进二进制；用户或发行包如果选择捆绑这些依赖，应另行遵守其许可证和服务条款。

本项目兼容经典 LabelPlus `.txt` 格式，并在产品方向、基础工作流和 OCR 后处理思路上参考过原 LabelMinus WPF 项目，但当前分支是 C++/Qt 独立实现，不包含原 WPF 项目源码。
