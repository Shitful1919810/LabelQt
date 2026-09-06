# LabelQt

LabelQt 是一个使用 C++/Qt 6 开发的跨平台 LabelPlus 文本工程编辑器，目标是在 Linux、Windows 与 macOS 上提供可用的漫画翻译标注工作流。

当前版本面向已有的经典 LabelPlus `.txt` 工程文件：程序可以打开文本工程，从工程文件所在目录加载图片，在图像预览区添加、移动和筛选标签，并将修改后的内容保存回 LabelPlus 文本格式。

本项目为独立实现，不包含原 WPF 项目的源代码。

## 功能概览

- 从图片文件夹新建工程，或打开和保存经典 LabelPlus `.txt` 工程文件。
- 支持选择多个 LabelPlus `.txt` 工程并按页合并，冲突页可在预览窗口中选择采用来源，并可在保存前调整最终页序。
- 支持在编辑菜单中调整当前工程的页面顺序，适配图片文件名与实际阅读顺序不一致的图源。
- 支持从命令行直接打开工程文件。
- 支持记录最近打开的工程；无命令行参数启动时会自动恢复最近一次工程。
- 左侧图像预览区支持缩放、翻页、点击添加标签。
- 支持按偏好设置中的修饰键拖动标签坐标，默认使用 `Ctrl`。
- 标签 marker 可按分组配置颜色、大小、字体大小和形状。
- 双击图像 marker 可在 marker 旁打开临时文本框编辑标签文本。
- 右侧标签列表支持文本和类别的原地编辑。
- 标签列表支持 `Shift` 连续多选、`Ctrl` 多选、批量删除和批量切换类别。
- 分组筛选同时作用于右侧标签列表和左侧图像预览区。
- 鼠标悬停在图像 marker 上时显示标签文本提示。
- 提供撤销/重做能力，覆盖新增、删除、移动、文本编辑、类别修改、标签排序和页面排序等工程编辑操作。
- 支持按间隔自动备份已修改的 LabelPlus 文本工程。
- 提供备份与恢复入口、外部保存冲突处理，以及带文件内容校验的重命名恢复。
- 页面管理支持累计待处理目标、局部撤销、范围与问题筛选，以及直接指定缺失图片。
- 支持工程全文查找、分组和页码范围、替换预览与整批撤销。
- 校对支持审阅状态、批注、人工配对与筛选后报告导出。具体操作与首版限制见 [工作流说明](docs/product-workflows.md)。
- 支持通过“自动化”菜单运行外部 Python 自动化脚本，当前内置测试脚本、OCR 与 AI 翻译脚本，并提供脚本开发 SDK。
- 偏好设置窗口支持通过系统字体选择器分别调整标签列表和大文本编辑框字体。
- 支持通过偏好设置启用内置 Breeze 风格 QSS 主题。
- 提供简体中文、繁体中文、日文和英文界面文本，并使用 Qt Linguist 工作流生成翻译资源。
- `preference.json` 支持对界面交互和 marker 样式做运行时调优。

## 当前状态

当前版本为 LabelQt 的首个正式版本，基础 LabelPlus 编辑、协作合并、页面管理、校对对比、自动化脚本、OCR 和 AI 翻译工作流已经具备可用形态。后续仍会继续打磨跨平台体验、发布打包和自动化脚本生态。

## 致谢

LabelQt 的早期产品方向、基础编辑工作流以及部分 OCR 流程设计参考了
[Yilibala-kid/LabelMinusinWPF](https://github.com/Yilibala-kid/LabelMinusinWPF)。本项目代码为 C++/Qt
重新实现，不包含原 WPF 项目的源代码。

本项目兼容经典 LabelPlus `.txt` 工程格式，但不是 LabelPlus 或 LabelMinus 的官方项目。

## 许可说明

本项目源码使用仓库内 `LICENSE.txt` 声明的 MIT 许可证。Qt 本身不属于本项目源码的一部分，使用和分发 Qt
时需要遵守 Qt 对应的开源或商业许可。

当前程序链接 Qt 6 的 `Core`、`Gui`、`Widgets`、`Concurrent` 模块；检测到 Qt Svg 时会额外链接 `Svg`，用于更完整地支持内置 Breeze 主题中的 SVG 图标。发布源码仓库时不要提交 Qt 源码或 Qt
二进制文件；发布二进制包时，应优先采用动态链接 Qt 的方式，并随包提供 Qt 使用声明、Qt 许可证文本、
Qt 模块与版本信息，以及其他第三方依赖的许可证说明。

项目计划同时发布 Windows 动态版和静态版。Windows 静态版只有在完成 Qt 与全部静态依赖的许可文本、
对应源码、重链接说明和干净环境验证后，才能作为正式发行产物。Linux 官方二进制发布物为 RPM；Linux
静态构建和 DEB 包仅保留为实验性方向。任何人分发静态链接 Qt 的产物时，都必须确认并满足 Qt LGPL、
GPL 或商业授权要求。

除非项目明确决定切换到 GPL 兼容的发布策略，否则不要引入 Qt 的 GPL-only 模块，例如 Qt Graphs、
Qt GRPC、Qt HTTP Server、Qt MQTT、Qt Virtual Keyboard、Qt Wayland Compositor 等。新增 Qt
模块前请先核对 Qt 官方许可文档。

仓库内置的 BreezeStyleSheets 主题资源位于 `resources/themes/breeze`，遵循 MIT 许可证；其中部分 SVG
图标资源带有 Apache License 2.0 notice。校对文本差异使用的 `diff-match-patch` 位于
`third_party/diff-match-patch`，遵循 Apache License 2.0。QtKeychain、内置主题资源、
官方 Python 脚本依赖和外部 API 的说明见 `THIRD_PARTY_NOTICES.md`。发布包应随带 `licenses/` 目录中的
第三方许可证文本；如果发布包选择捆绑 Python 运行时、OCR 模型或 Python wheel，还需要额外附带这些组件自己的
许可证与 notice。

## 环境要求

- CMake 3.24 或更高版本。
- Ninja，或 Windows 上的 Visual Studio 2022 生成器。
- 支持 C++23 的编译器。
- Qt 6.5 或更高版本，至少需要 Qt Core、Gui、Widgets、Concurrent；建议安装 Qt Svg 以完整显示内置 Breeze 主题图标；开发翻译资源时还需要 Qt Linguist Tools。
- QtKeychain，用于通过系统密钥环保存自动化脚本所需的 API key 等敏感信息。
- Python 3 为自动化脚本运行时依赖；只使用主程序基础编辑功能时不是必需项。

## 构建

推荐使用仓库内置的 CMake Presets。Debug 与 Release 均已提供跨平台 preset。

### 获取源码和子模块

校对文本差异依赖以 Git submodule 形式引入的 `diff-match-patch`。首次获取源码时，推荐直接递归克隆：

```bash
git clone --recurse-submodules https://github.com/Shitful1919810/LabelQt.git
cd LabelQt
```

如果已经克隆了仓库，或者切换到了另一个 tag/分支，请在配置 CMake 前初始化并同步子模块：

```bash
git submodule update --init --recursive
```

源码归档或未包含 submodule 内容的工作副本无法直接构建；此时应重新获取完整仓库，或手动将与当前提交匹配的
`diff-match-patch` 内容放在 `third_party/diff-match-patch`。不要使用任意版本覆盖父仓库记录的 submodule
提交，否则文本差异行为和回归测试结果可能不一致。

### 用户手册

仓库的 `manual/` 目录是独立的 Typst 用户手册子工程，不参与主程序 CMake 构建。安装 Typst 0.14 或更高版本后，可生成中文 PDF 手册：

```bash
python scripts/build_manual.py
```

默认输出到 `dist/LabelQt-user-manual-zh_CN.pdf`。手册截图素材放在 `manual/assets/`，缺少截图时会使用占位框，不影响编译。正式发布时建议把编译好的 PDF 作为独立 release 附件上传。

### Linux

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Release 构建：

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

正式预发行 RPM 应在配置时传入版本通道和目标发行版，例如
`-DLABELQT_PRERELEASE=alpha.4 -DLABELQT_RPM_DIST_TAG=fc44`。支持 `alpha.N`、`beta.N`、`rc.N`；正式版将
`LABELQT_PRERELEASE` 留空。

生成正式发布使用的 Linux RPM：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G RPM
```

RPM 应在目标 Fedora/openSUSE 系环境中构建和验证。仓库仍保留 DEB 生成器用于实验和社区测试，但 DEB
不属于受支持的正式发布矩阵。默认 `package` 目标只生成 RPM；需要让它同时生成实验性 DEB 时，可在配置
阶段开启 `-DLABELQT_ENABLE_EXPERIMENTAL_DEB_PACKAGE=ON`，也可以单独运行：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G DEB
```

生成实验性 `.deb` 通常需要 Debian/Ubuntu 环境中的 `dpkg-dev` / `fakeroot`，生成正式 `.rpm` 通常需要
`rpm-build`。安装包会包含
`labelqt` 可执行文件、桌面入口、图标、许可证和第三方声明。默认偏好设置由程序内置提供；Python、OCR 模型、
Python 运行时、OCR 模型与自动化脚本依赖不会被打包。默认会把官方自动化脚本安装到
`/usr/share/labelqt/scripts/official`；配置时传入 `-DLABELQT_INSTALL_OFFICIAL_AUTOMATION_SCRIPTS=OFF`
可以生成完全不含自动化脚本的原生包。Linux 静态构建目前没有受支持的正式目标，后续即使加入也应先作为
实验性便携构建验证。

### Windows

默认 Windows preset 使用 Ninja：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
cmake --build --preset windows-debug --target deploy_windows
ctest --preset windows-debug
```

Release 构建：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
cmake --build --preset windows-release --target deploy_windows
```

如果不想安装 Ninja，也可以使用 Visual Studio 2022 生成器 preset：

```powershell
cmake --preset windows-vs-debug
cmake --build --preset windows-vs-debug
cmake --build --preset windows-vs-debug --target deploy_windows
ctest --preset windows-vs-debug
```

Visual Studio Release 构建：

```powershell
cmake --preset windows-vs-release
cmake --build --preset windows-vs-release
cmake --build --preset windows-vs-release --target deploy_windows
```

制作 Windows 预编译发布包时，以 `deploy_windows` 生成后的 Release 目录为准，保留 `labelqt.exe`、运行所需
DLL 和 Qt 插件目录，去掉测试程序 `LabelQtTests.exe` 与 `Qt6Test.dll`。仓库根目录的 `LICENSE.txt` 和
`THIRD_PARTY_NOTICES.md` 以及 `licenses/` 目录中的第三方许可证文本应随包发布。推荐使用统一打包脚本：

```powershell
python scripts/package_windows_release.py --source path\to\Release --version v0.1.0 `
  --variant shared --dependency-license-dir path\to\vcpkg\installed\x64-windows\share `
  --qt-version 6.x.y --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0/qt-everywhere-src-6.x.y.tar.xz `
  --exclude-automation-scripts `
  --output-dir dist
```

如果需要生成带安装向导的 Windows MSI 安装包，可在 Windows 上安装 WiX Toolset v4+ 及其 UI 扩展，然后运行：

```powershell
python scripts/package_windows_msi.py --source path\to\Release --version v0.1.0 `
  --dependency-license-dir path\to\vcpkg\installed\x64-windows\share `
  --qt-version 6.x.y --qt-source-url https://github.com/Shitful1919810/LabelQt/releases/download/v0.1.0/qt-everywhere-src-6.x.y.tar.xz `
  --exclude-automation-scripts `
  --output-dir dist
```

Windows 静态构建：

```powershell
cmake --preset windows-static-release
cmake --build --preset windows-static-release
```

如果希望使用 Visual Studio 2022 生成器，而不是 Ninja：

```powershell
cmake --preset windows-vs-static-release
cmake --build --preset windows-vs-static-release
```

这些 preset 在独立构建目录中使用同一个 `LabelQt` 应用目标，并要求当前 CMake 能找到静态构建的 Qt。使用普通动态 Qt
安装包时，CMake 会直接报错，因为动态 Qt 无法生成真正脱离 Qt DLL 的单 exe。即便 Qt 本身是静态构建，
QtKeychain 等第三方依赖也需要提供静态库，否则最终产物仍可能依赖额外 DLL。Windows 静态版
计划作为正式 release 产物，但发布前必须完成静态依赖审计、Qt 许可与重链接材料准备，并在干净 Windows
环境中验证。静态版既可以使用 `package_windows_release.py --variant static` 生成完整 ZIP，也可以发布单 exe，
并额外提供包含精确依赖许可证、Qt 对应源码、vcpkg 补丁和重链接说明的合规材料包；不能只发布一个没有
许可证和对应源码入口的裸 exe。详细流程见 `docs/windows-static-relinking.md`。若使用 Ninja preset，请先进入 “x64 Native Tools Command Prompt for
VS 2022” 或手动设置 MSVC 编译器环境；VS preset 则会直接使用 Visual Studio 2022 生成器。

### macOS

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

Release 构建：

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

如果本机编译器经由 ccache 转发，且 CMake 在检测 `Threads` 时失败，可以临时禁用 ccache 后重试：

```bash
cmake -E env CCACHE_DISABLE=1 cmake --preset linux-debug
cmake -E env CCACHE_DISABLE=1 cmake --build --preset linux-debug
cmake -E env CCACHE_DISABLE=1 ctest --preset linux-debug
```

## 运行

Linux Debug 构建完成后，可直接运行：

```bash
./build/linux/debug/src/labelqt
```

也可以在启动时传入 LabelPlus 文本工程路径：

```bash
./build/linux/debug/src/labelqt /path/to/project.txt
```

Linux Release 构建对应路径为：

```bash
./build/linux/release/src/labelqt
```

Windows 构建会生成 GUI 可执行文件，macOS 构建会生成应用包。

### Windows Qt 运行时

Windows 上直接运行刚编译出的 `labelqt.exe` 时，如果系统找不到 `Qt6Widgets.dll`、`Qt6Core.dll`、
`Qt6Gui.dll` 等文件，说明 Qt 运行时 DLL 还没有部署到 exe 旁边，或 Qt 的 `bin` 目录不在 `PATH` 中。

推荐在构建后运行项目提供的部署目标：

```powershell
cmake --build --preset windows-debug --target deploy_windows
cmake --build --preset windows-release --target deploy_windows
```

Visual Studio preset 对应：

```powershell
cmake --build --preset windows-vs-debug --target deploy_windows
cmake --build --preset windows-vs-release --target deploy_windows
```

该目标会调用 Qt 自带的 `windeployqt`，把运行所需的 Qt DLL 和平台插件复制到 `labelqt.exe`
所在目录。之后从该目录启动 exe 即可。若 CMake 提示找不到 `windeployqt`，请把 Qt 安装目录下的
`bin` 目录加入 `PATH`，例如 `C:\Qt\6.x.x\msvc2022_64\bin`。

## 偏好设置

运行时界面偏好由 `AppPreferences` 统一读取。程序会先查找可执行文件目录旁边的 `preference.json`，再查找系统用户配置目录中的 `preference.json`。如果两者都不存在，则使用 C++ 内置默认值。无效或损坏的配置会回退到默认值，并在状态栏显示常驻警告。

也可以在程序内通过“文件 > 偏好设置”打开偏好设置窗口，使用结构化表单编辑常用选项，或直接调用系统文本编辑器打开 `preference.json`。

完整示例见仓库根目录 `preference.json`。便携版可以把这份文件放在 exe 旁边；Linux 安装包通常不安装这份可编辑配置。如果没有 exe 旁边的配置文件，用户保存偏好设置时会写入用户配置目录。下面片段展示主要结构：

```json
{
  "appearance": {
    "language": "",
    "style": "",
    "theme": ""
  },
  "automation": {
    "python": {
      "arguments": [],
      "autoInstallRequirements": false,
      "command": "",
      "pipIndexUrl": ""
    },
    "shortcuts": {},
    "showRunLog": false
  },
  "backupPath": "bak",
  "backupIntervalSeconds": 60,
  "labelMarker": {
    "diameter": 20.0,
    "fontPointSize": 10.0
  },
  "labelTable": {
    "fontFamily": "",
    "fontPointSize": 0.0,
    "maxTextRows": 3
  },
  "labelTextEditor": {
    "fontFamily": "",
    "fontPointSize": 0.0
  },
  "markerTextBubble": {
    "fontFamily": "",
    "fontPointSize": 0.0,
    "opacity": 1.0
  },
  "canvasLabelTextEditor": {
    "opacity": 1.0
  },
  "input": {
    "moveLabelModifier": "ctrl",
    "previousLabelModifier": "ctrl",
    "nextLabelShortcut": "Tab",
    "alternatePreviousLabelShortcut": "Ctrl+Up",
    "alternateNextLabelShortcut": "Ctrl+Down",
    "previousPageShortcut": "Alt+Left",
    "nextPageShortcut": "Alt+Right",
    "editLabelTextShortcut": "Return",
    "commitLabelTextShortcut": "Ctrl+Return",
    "undoShortcut": "Ctrl+Z",
    "redoShortcut": "Ctrl+Y"
  },
  "groupStyles": [
    {
      "groupColor": "#ef4444",
      "markerDiameter": 20.0,
      "fontPointSize": 10.0,
      "markerStyle": "circle"
    },
    {
      "groupColor": "#2563eb",
      "markerDiameter": 20.0,
      "fontPointSize": 10.0,
      "markerStyle": "square"
    },
    {
      "groupColor": "#10b981",
      "markerDiameter": 20.0,
      "fontPointSize": 10.0,
      "markerStyle": "circle"
    }
  ]
}
```

字段说明：

- `appearance.style`：启动时强制使用的 Qt 控件风格名称，例如 `Fusion`。为空时不强制设置，使用系统默认风格。可选值由当前 Qt 环境的 `QStyleFactory::keys()` 决定，偏好设置窗口会自动列出可用 style。
- `appearance.theme`：应用内置 Breeze QSS 样式表主题。为空时不使用样式表；当前支持 `breezeDark`、`breezeLight`。该字段与 `appearance.style` 可同时使用，程序会先设置 Qt style，再叠加 Breeze QSS。
- `appearance.language`：界面语言。为空时跟随系统语言；可选值由程序扫描当前可用的 `labelqt_*.qm` 翻译资源得到。修改后需要重启应用程序生效。
- `automation.showRunLog`：运行自动化脚本时是否显示 stdout/stderr 日志窗口。
- `automation.python.command`：自动化脚本使用的 Python 命令或可执行文件路径。为空时尝试使用内置候选或系统 Python。
- `automation.python.arguments`：传给 Python 解释器的额外参数。
- `automation.python.autoInstallRequirements`：运行脚本前是否尝试安装该脚本目录下的 `requirements.txt`。
- `automation.python.pipIndexUrl`：安装依赖时传给 pip 的镜像源 URL。为空时使用 pip 默认源。
- `automation.shortcuts`：用户为自动化脚本分配的快捷键，键为脚本稳定 id，值为 Qt portable key sequence。
- `labelMarker.diameter`：默认 marker 直径，单位为屏幕像素，支持浮点数。
- `labelMarker.fontPointSize`：默认 marker 内部序号字号，使用 Qt 字号单位，支持浮点数。
- `labelTable.maxTextRows`：右侧标签列表文本列自动换行后的最大显示行数。
- `labelTable.fontFamily`：右侧标签列表字体。为空时使用系统默认字体。
- `labelTable.fontPointSize`：右侧标签列表字号。为 `0` 时使用系统默认字号。
- `labelTextEditor.fontFamily`：右下角大文本编辑框字体。为空时使用系统默认字体。
- `labelTextEditor.fontPointSize`：右下角大文本编辑框字号。为 `0` 时使用系统默认字号。
- `markerTextBubble.fontFamily`：图像 marker 文本气泡字体。为空时使用系统默认字体。
- `markerTextBubble.fontPointSize`：图像 marker 文本气泡字号。为 `0` 时使用系统默认字号。
- `markerTextBubble.opacity`：图像 marker 文本气泡不透明度，范围 `0.0` 到 `1.0`，默认 `1.0`。
- `canvasLabelTextEditor.opacity`：双击 marker 打开的临时文本编辑框不透明度，范围 `0.0` 到 `1.0`，默认 `1.0`。
- `input.moveLabelModifier`：拖动图像 marker 时需要按住的修饰键，默认 `ctrl`。
- `input.previousLabelModifier`：反向切换标签时需要按住的修饰键，默认 `ctrl`。
- `input.nextLabelShortcut`：在主窗口内切换到下一个可见标签的快捷键，默认 `Tab`。按住 `input.previousLabelModifier` 再触发该快捷键会切换到上一个可见标签。
- `input.alternatePreviousLabelShortcut`：在主窗口内切换到上一个可见标签的额外快捷键，默认 `Ctrl+Up`。
- `input.alternateNextLabelShortcut`：在主窗口内切换到下一个可见标签的额外快捷键，默认 `Ctrl+Down`。
- `input.previousPageShortcut`：在主窗口内切换到上一页的快捷键，默认 `Alt+Left`。
- `input.nextPageShortcut`：在主窗口内切换到下一页的快捷键，默认 `Alt+Right`。
- `input.editLabelTextShortcut`：焦点位于右侧标签列表时进入当前标签文本原地编辑的快捷键，默认 `Return`。
- `input.commitLabelTextShortcut`：焦点位于标签文本原地编辑器时提交并退出编辑的快捷键，默认 `Ctrl+Return`。
- `input.undoShortcut`：撤销快捷键，使用 Qt portable key sequence 文本格式，默认 `Ctrl+Z`。
- `input.redoShortcut`：重做快捷键，使用 Qt portable key sequence 文本格式，默认 `Ctrl+Y`，可改为 `Ctrl+Shift+Z` 等组合键。
- `backupPath`：自动备份目录，默认 `bak`。相对路径会解析到当前工程文件所在目录下，绝对路径会直接使用。
- `backupIntervalSeconds`：自动备份检查间隔，单位为秒，默认 60。
- `backupMaximumCount`：每工程自动备份上限，0 表示全部保留；不清理手工备份或未解决事务材料。
- `input.findProjectShortcut`、`input.nextSearchMatchShortcut`、`input.previousSearchMatchShortcut`：工程查找、下一匹配、上一匹配，默认 `Ctrl+F`、`F3`、`Shift+F3`。
- `groupStyles`：按分组顺序应用的分组样式数组。

当有工程打开且存在未保存修改时，程序会按 `backupIntervalSeconds` 检查是否需要备份。每次备份会在 `backupPath` 中生成一份 LabelPlus 文本工程副本，文件名由当前工程文件名和时间戳组成。

`groupStyles` 中每一项可包含：

- `groupColor`：分组代表色，用于图像 marker、插入分组下拉框、分组筛选菜单和标签列表类别列。
- `markerDiameter`：该分组 marker 的直径。
- `fontPointSize`：该分组 marker 内部序号字号。
- `markerStyle`：marker 形状，当前支持 `circle` 和 `square`。

如果某个分组没有对应的 `groupStyles` 项，图像 marker 会使用黑色圆形和默认大小，文本界面保留默认文字颜色。

## 自动化脚本

程序会优先读取可执行文件目录旁边的自动化脚本；如果没有便携脚本目录，则读取安装到系统数据目录的官方脚本。用户自定义脚本只从用户脚本目录读取，可以通过“自动化 > 打开用户脚本目录”打开该目录：

```text
scripts/official    官方脚本
scripts/custom      用户自定义脚本
```

每个脚本独立放在一个目录中，目录内至少包含 `script.json` 和入口 Python 文件，也可以附带 `requirements.txt` 与资源文件。`script.json` 示例：

```json
{
  "apiVersion": 1,
  "name": "Label Word Count",
  "entry": "word_count.py",
  "description": "Count characters in all non-deleted labels in the current project."
}
```

一个脚本目录也可以在同一个 `script.json` 中定义多个菜单项。此时程序会在“自动化”菜单中为这个目录生成子菜单：

```json
{
  "apiVersion": 1,
  "name": "OCR",
  "description": "Configure OCR and run OCR scripts.",
  "scripts": [
    {
      "id": "configure",
      "name": "Configure OCR",
      "entry": "configure_ocr.py",
      "parameters": [
        {
          "key": "engine",
          "label": "Default OCR engine",
          "type": "choice",
          "default": "paddle",
          "options": ["paddle", "manga-with-paddle"]
        }
      ]
    },
    {
      "id": "preview",
      "name": "OCR Preview",
      "entry": "ocr_preview.py"
    },
    {
      "id": "add_labels",
      "name": "OCR Add Labels",
      "entry": "ocr_preview.py",
      "environment": {
        "LABELQT_OCR_ACTION": "add-labels"
      }
    },
    {
      "id": "add_page_range_labels",
      "name": "OCR Add Page Range Labels",
      "entry": "ocr_preview.py",
      "parameters": [
        {
          "key": "startPage",
          "label": "Start page (1-based)",
          "type": "text",
          "default": "1"
        },
        {
          "key": "endPage",
          "label": "End page (1-based)",
          "type": "text",
          "default": "1"
        }
      ],
      "environment": {
        "LABELQT_OCR_ACTION": "add-page-range-labels"
      }
    }
  ]
}
```

`environment` 会被注入到对应脚本进程环境中，适合让多个菜单项复用同一个 Python 入口但采用不同运行参数。

每个脚本还可以声明 `parameters`。只要参数列表非空，运行前主程序会弹出 Qt 参数窗口，把用户填写的值写入 `input.json` 的 `parameters` 字段。当前支持的参数类型包括：

- `text`：普通文本输入框。
- `textarea` / `multiline`：多行文本输入框，适合较长的 prompt 或说明文本。
- `group`：当前工程分组下拉框。
- `choice` / `select` / `enum`：通过 `options` 提供固定选项。
- `boolean` / `bool`：复选框。
- `file`：文件路径输入框，附带文件选择按钮。
- `directory`：目录路径输入框，附带目录选择按钮。
- `secret`：密码输入框。用户填写的值不会写入 `input.json`，而是由主程序保存到系统 keychain。

例如：

```json
{
  "key": "device",
  "label": "PaddleOCR device",
  "type": "choice",
  "default": "cpu",
  "options": ["cpu", "gpu"]
}
```

`secret` 参数需要额外声明 keychain 元数据：

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

真正运行时需要读取密钥的脚本，应在对应脚本条目上声明 `secrets`。主程序会从系统 keychain 读取该密钥，并只注入到子进程环境变量中：

```json
{
  "id": "preview",
  "name": "Translation Preview",
  "entry": "translate.py",
  "secrets": [
    {
      "key": "deepseekApiKey",
      "label": "DeepSeek API key",
      "service": "LabelQt",
      "account": "deepseek_api_key",
      "environment": "DEEPSEEK_API_KEY",
      "required": true
    }
  ]
}
```

脚本内只需要读取对应环境变量，例如 `DEEPSEEK_API_KEY`。不要把 API key 写入 `script.json`、`config.json`、`input.json` 或日志。

更完整的脚本开发说明、输入/输出 JSON 格式和最小 SDK 用法见 [docs/automation-scripting.md](docs/automation-scripting.md)。

运行脚本时，程序会通过外部 Python 进程调用：

```bash
python script.py --input input.json --output output.json
```

`input.json` 包含当前工程快照、当前页面与选中标签上下文、当前图像选区，以及用户在参数窗口填写的 `parameters`，`output.json` 返回脚本摘要、结果和可选的结构化 `operations`。如果 `output.json` 顶层包含 `"quiet": true`，脚本成功结束后不会弹出结果窗口，只会更新状态栏。脚本不应直接修改 LabelPlus 工程文件；需要修改工程时，应输出 `operations`，由主程序校验后应用到工程并注册撤销/重做。

输入快照默认只包含未删除 label。每个 label 中的 `labelIndex` 是工程内部 label 下标，供未来 `operations` 精确引用；`visibleIndex` 是过滤掉已删除 label 后的可见序号，仅用于展示。

`context` 描述当前 UI 上下文：

```json
{
  "currentPage": {
    "hasPage": true,
    "index": 0,
    "number": 1,
    "name": "001.png",
    "imagePath": "/path/to/001.png"
  },
  "selectedLabelIndexes": [0, 3],
  "selectedLabels": [
    {
      "labelIndex": 0,
      "visibleIndex": 1,
      "group": "框内",
      "x": 0.5,
      "y": 0.5,
      "text": "..."
    }
  ]
}
```

`currentPage.index` 是从 `0` 开始的内部页下标；`currentPage.number` 是从 `1` 开始、适合显示给用户的页码。`selectedLabelIndexes` 和 `selectedLabels` 均只描述当前页的未删除已选标签，支持多选。

`project.pages[].imagePath` 是每页图片路径；`project.imagePaths` 额外提供工程内所有图片路径列表，便于脚本批量处理。`selection` 描述当前图像预览区的选区：

```json
{
  "hasSelection": true,
  "imageIndex": 0,
  "page": "001.png",
  "imagePath": "/path/to/001.png",
  "rect": {
    "x": 0.12,
    "y": 0.34,
    "width": 0.2,
    "height": 0.1,
    "left": 0.12,
    "top": 0.34,
    "right": 0.32,
    "bottom": 0.44
  }
}
```

选区坐标均为相对当前图片的归一化坐标，范围为 `0.0` 到 `1.0`。如果当前没有选区，`selection.hasSelection` 为 `false`。

当前支持的修改操作包括：

修改 label 类别：

```json
{
  "type": "setLabelGroup",
  "page": "012.png",
  "labelIndex": 3,
  "group": "框外"
}
```

修改 label 文本：

```json
{
  "type": "setLabelText",
  "page": "012.png",
  "labelIndex": 3,
  "text": "新的文本内容"
}
```

修改 marker 坐标：

```json
{
  "type": "setLabelPosition",
  "page": "012.png",
  "labelIndex": 3,
  "x": 0.4,
  "y": 0.6
}
```

删除 label：

```json
{
  "type": "deleteLabel",
  "page": "012.png",
  "labelIndex": 3
}
```

新增 label：

```json
{
  "type": "addLabel",
  "page": "012.png",
  "group": "框内",
  "text": "识别出的文本",
  "x": 0.75,
  "y": 0.25
}
```

其中 `x` 和 `y` 是归一化图片坐标。主程序会校验页名、标签下标和分组名，应用成功后统一注册到撤销/重做栈。

仓库提供的官方示例脚本包括：

- `scripts/official/test`：仅供开发构建测试自动化框架和展示推荐写法，会在本地自动化菜单中显示为 `Test` 子菜单；正式 ZIP、MSI 和 RPM 会排除该目录。
- `scripts/official/ocr_preview`：对当前选区、当前页或指定页码区间运行 OCR。`Configure OCR` 会通过参数窗口写入用户配置目录下的本机配置，用于保存 OCR 引擎、语言、设备、本地 manga-ocr 模型目录、默认标签分组、排序方向以及非预览入口完成后是否弹出结果报告等设置。预览入口会展示后处理后的文本块；生成标签入口会复用同一套后处理结果并输出 `addLabel` 操作。选区 OCR 会合并为一个 label，整页和跨页 OCR 会为每个文本块生成一个 label。
- `scripts/official/ai_translate`：通过 DeepSeek API 翻译当前选中的 label；如果没有选中 label，则翻译当前页所有 label。`Configure AI Translation` 会将 API key 保存到系统 keychain，并在用户配置目录下保存 API base URL、模型、目标语言、翻译风格、自定义 prompt 以及应用翻译后是否弹出结果报告等非敏感设置。预览入口只展示译文或译文加分析；应用入口会输出 `setLabelText` 操作并由主程序注册撤销/重做；跨页入口会让用户输入 1-based 页码区间，并把整个区间的 label 一次性提交给模型作为上下文，再应用返回的译文。

自动化脚本来源分为一个官方来源和一个用户来源。官方来源优先使用 exe 旁边的 `scripts/official`，否则使用 Linux 安装包中的 `/usr/share/labelqt/scripts/official`；用户来源优先使用 exe 旁边的 `scripts/custom`，否则使用程序打开的用户脚本目录。这样便携版可以直接携带脚本，RPM（以及实验性 DEB）用户也能看到系统安装的官方脚本，同时不会重复加载两套 official 或 custom。

## 开发检查

提交前建议运行：

```bash
python scripts/check_translations.py
cmake --build --preset linux-debug --target release_translations
cmake --build --preset linux-debug
ctest --preset linux-debug
```

格式化 C++ 源码：

```bash
find src tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i
```

## 国际化

项目使用 Qt 推荐的 Linguist 工作流：

- 新增用户可见 UI 文本时使用 `tr()`。
- 同步更新 `translations/` 下的四份 `labelqt_*.ts` 翻译文件。
- 使用 `release_translations` 生成 `.qm` 翻译资源。
- 程序启动时会根据系统区域设置自动加载匹配翻译。

## 项目结构

```text
src/core        平台无关的数据模型、LabelPlus 解析与保存、偏好设置、撤销栈
src/ui          Qt Widgets 用户界面
src/services    工程工作流、会话状态、自动备份、OCR 与图片缓存等服务层
translations    Qt Linguist 翻译源文件
tests           Qt Test 单元测试
docs            架构与开发文档
scripts         开发辅助脚本，以及 official/custom 自动化脚本目录
```

更多开发约定请参见：

- `CONTRIBUTING.md`
- `docs/architecture.md`
- `docs/code-walkthrough.md`
- `AGENTS.md`
