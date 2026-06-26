# 依赖与移植说明

这份文档记录 LabelQt 当前需要的构建依赖、运行依赖、可选增强项和发布时需要注意的边界。它不是许可证全文，发布前仍应以各依赖上游许可证为准。

## C++ 构建要求

必需：

- CMake 3.24 或更新版本。
- 支持 C++23 的编译器。
- Qt 6.5 或更新版本：
  - `Core`
  - `Gui`
  - `Widgets`
- QtKeychain。

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
- LibArchive：可用时启用压缩包读取能力，支持 zip、7z、rar 等多种格式；不可用时相关能力受限。
- Qt Test：测试目标使用，普通运行不需要。

当前没有引入 Qt Concurrent。图片加载缓存使用项目自己的 `ImagePageCache`。

## 自动化脚本运行时

主程序不嵌入 Python。自动化脚本通过外部 Python 进程运行：

```bash
python script.py --input input.json --output output.json
```

因此：

- 不使用自动化脚本时，LabelQt 主程序本体不需要 Python。
- 使用自动化脚本时，需要可用的 Python 3。
- Python 命令、解释器参数、是否自动安装 `requirements.txt`、pip 镜像源都在偏好设置中配置。
- 自动安装依赖会复用自动化日志窗口，并支持用户取消。

官方脚本依赖：

- `scripts/official/test`：只使用 Python 标准库。
- `scripts/official/ai_translate`：只使用 Python 标准库，调用 DeepSeek 兼容 API；API key 由主程序通过 QtKeychain 存入系统密钥环并注入环境变量。
- `scripts/official/ocr_preview`：
  - `pillow`
  - `paddleocr`
  - `manga-ocr`
  - `transformers<5`

OCR 和 AI 翻译属于可选脚本能力。它们的 Python 包、模型文件、API 服务和用户凭据不随本仓库源码自动获得，也不属于 C++ 主程序许可证的一部分。

## 运行时文件布局

构建后，CMake 会把这些运行时文件同步到可执行文件目录：

```text
preference.json
scripts/official/
scripts/custom/
```

`scripts/custom` 默认只创建空目录，用户脚本不应提交到仓库。

翻译文件不需要额外复制：构建时 `qt_add_translations()` 会把 `labelqt_*.qm` 编进资源路径 `/i18n`。`TranslationManager` 仍支持从可执行文件旁边的 `i18n` 目录加载翻译，方便以后做外部语言包。

## Windows 发布注意事项

动态 Qt 构建需要部署 Qt 运行时。项目提供 `deploy_windows` 目标，会调用 `windeployqt` 把 Qt DLL、平台插件和相关资源复制到 exe 旁边：

```powershell
cmake --build --preset windows-vs-release --target deploy_windows
```

如果使用 vcpkg 或自建 Qt，需确认 CMake 找到的是期望的 Qt 包。动态 Qt 会生成依赖 Qt DLL 的 exe，这是正常情况。

制作 Windows 预编译发布包时，应以 `deploy_windows` 生成后的 Release 目录为基础：

- 保留 `labelqt.exe`、运行所需 DLL、Qt 插件目录、`preference.json`、`scripts/official` 和空的 `scripts/custom`。
- 删除 `LabelQtTests.exe` 与 `Qt6Test.dll`，它们只属于测试目标。
- 附带仓库根目录的 `LICENSE.txt` 和 `THIRD_PARTY_NOTICES.md`。
- 不要把 Qt SDK、Qt 源码、构建缓存、`compile_commands.json`、用户脚本配置或 API key 打进发布包。

仓库还提供 `LabelQtStatic` / `windows-vs-static-release` 等实验目标，但它们只适合本地验证：

- 必须使用真正静态构建的 Qt。
- QtKeychain、LibArchive 等依赖也需要有静态库，否则仍可能出现额外 DLL。
- 未做 Qt 许可证审查前，不要把静态单 exe 当作官方发行物。

## Linux DEB/RPM 打包

普通 Linux release 构建生成的是动态链接可执行文件。项目现在使用 CPack 生成 Linux 原生安装包：

```bash
cmake --build --preset linux-release --target package
```

该目标会根据 `cmake/Packaging.cmake` 生成 `.deb` 和 `.rpm`。如果只想生成其中一种格式，可以直接调用
构建目录中的 CPack 配置：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G DEB
cpack --config build/linux/release/CPackConfig.cmake -G RPM
```

打包工具要求：

- `.deb`：通常需要 `dpkg-dev`、`fakeroot`，并依赖 `dpkg-shlibdeps` 自动分析动态库依赖。
- `.rpm`：通常需要 `rpm-build`。

安装包内容：

- `/usr/bin/labelqt`
- `/usr/bin/preference.json`
- `/usr/bin/scripts/official`
- `/usr/bin/scripts/custom` 空目录
- `/usr/share/applications/org.labelqt.LabelQt.desktop`
- `/usr/share/icons/hicolor/scalable/apps/org.labelqt.LabelQt.svg`
- `/usr/share/doc/labelqt/LICENSE.txt`
- `/usr/share/doc/labelqt/THIRD_PARTY_NOTICES.md`

DEB/RPM 不会把 Python 解释器、OCR 模型或自动化脚本依赖打包进去；自动化脚本依赖仍由用户按脚本目录中的
`requirements.txt` 安装。DEB/RPM 也不会捆绑整套 Qt 运行库，实际依赖由目标发行版的包管理器解析和安装。
因此，最好分别在 Debian/Ubuntu 系和 Fedora/openSUSE 系环境里构建并测试对应格式。

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
- 压缩包：LibArchive。
- OCR/AI：外部 Python 脚本和 JSON 桥接，不嵌入到 C++ 主程序。

新增依赖前必须检查：

- 是否跨平台。
- 是否与 MIT 项目和 Qt LGPL 动态链接发布策略兼容。
- 是否需要在 `THIRD_PARTY_NOTICES.md`、README 或发行包中附带第三方声明。
- 是否会把 GPL-only 义务传导给整个发布包。

## 许可与致谢边界

LabelQt 源码使用仓库根目录 `LICENSE.txt` 的 MIT 许可证。第三方内容分三类：

- 链接或运行时依赖：Qt、QtKeychain、LibArchive 等，发布二进制时需要带对应第三方声明。
- 仓库内置资源：BreezeStyleSheets 位于 `resources/themes/breeze`，本地保留 MIT 和 Apache 2.0 声明。
- 用户运行时安装/调用的脚本依赖和服务：PaddleOCR、manga-ocr、Pillow、Transformers、DeepSeek API 等，不随 C++ 主程序一起编译进二进制；用户或发行包如果选择捆绑这些依赖，应另行遵守其许可证和服务条款。

本项目兼容经典 LabelPlus `.txt` 格式，并在产品方向、基础工作流和 OCR 后处理思路上参考过原 LabelMinus WPF 项目，但当前分支是 C++/Qt 独立实现，不包含原 WPF 项目源码。
