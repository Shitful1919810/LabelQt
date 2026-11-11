# Android 技术预览：构建与测试

此分支开始实现 [完整移植方案](android-port-plan.md)，当前不是正式 Android 发行版。
桌面仍使用 Widgets；移动界面使用 Qt Quick，复用工程格式、身份、编辑命令、撤销、持久化和校对服务。

普通用户请先读 [使用说明](android-user-guide.md)；开发与 Agent 协作请读
[工程约定](android-development.md)。本文负责构建、资源限制和验证记录。

## 当前可用范围

- ZIP、7z、RAR/RAR5、POSIX tar、tar.gz / TXT 导入为独立本机副本；不会改写 QQ 下载的原文件。
- 按文件头识别归档，支持扩展名误写为 `.zip` 的 RAR。没有 TXT 的原图包自动新建工程，
  按相对路径自然排序、保留子目录，复用共享默认分组和稳定身份；不跨压缩包自动合并。
- 启动首页直接展示本地工程及导入入口，可直接打开、重命名、删除；打开工程后仍可从左上角抽屉切换。
  首次扫描在后台进行，首页与抽屉使用同一个模型和列表组件。
- 工程列表抽屉提供右上角关闭按钮；也可点击抽屉外区域或使用返回键关闭，不必切换工程。
- 每个工程右侧菜单可重命名或删除本机副本；灰色时间表示正文最近成功保存的时间。
  改显示名不会改 TXT、图片文件名或工程身份；删除需确认且不可撤销，不删除原始导入文件或已分享的 ZIP。
- 新导入的工程标题采用来源压缩包名称（去掉扩展名），Android 从文件提供者读取显示名，
  不把私有暂存文件的随机名称当成工程名。已导入的旧工程标题不会自动改写。
- 手机图像/标签分页与宽屏并列布局，分页、缩放、平移、重叠标记选择。
- 工具按钮使用随包附带的 SVG，不依赖手机字体的特殊符号。标记可见主体为 22 dp，触摸热区保持 48 dp。
- marker 按工程完整分组顺序使用 `AppPreferences.groupStyles` 的颜色和圆形/方形，筛选不会改变颜色对应关系。
  选中只强调外圈，不覆盖分组颜色；尺寸保持移动端触摸设计。未配置分组沿用桌面的黑色圆形回退规则。
- 两指缩放后必须全部抬指才能开始新手势；剩余一指不会接管为拖动或提交标签移动。
- 文本编辑时收起分页、视图切换和画布工具栏，显示图像与有界高度的滚动编辑区。
  顶部键盘按钮结束编辑并收起输入法；列表中进入编辑也会显示图像。结束编辑后恢复导航控件。
- 单标签新增、删除、文本和分组编辑、标记移动，撤销/重做与分组筛选、新增分组。
- 编辑后延迟自动保存，切换工程和导出前保存，进入后台时尝试提交输入并保存。
- 开始/结束校对、标签/页面批注、解决批注、文本差异预览。复用桌面校对轮次，迟到的 diff 结果不能覆盖新会话。
- 快照 ZIP 导出，Android 文件选择器、接收 VIEW/SEND 和只读系统分享桥接。
- 分享前可修改 ZIP 文件名，默认使用原导入包的名称并改为 `.zip`；拒绝路径和不安全字符。
  同名导出互不覆盖。旧工程没有原 ZIP 名记录时，使用已有工程标题作为默认名。

目前有意不展示未接通的入口：页面管理/改名/补图、工程合并、直接从图片目录新建、全文替换、标签多选排序、
选区截图、外部工程比较/人工配对、独立 TXT/HTML 导出、完整偏好与恢复草稿界面。
Python 不属于 Android 计划范围。上述其他能力仍在完整首版范围内，不能视为已取消。

预览限制：

- 一个压缩包最多有一个工程 TXT；多 TXT 包明确拒绝，不猜测哪个是译稿。
  没有 TXT 时扫描支持的图片；TXT 无效则报错，不静默新建。暂不支持 ZIP64、加密、分卷、自解压程序。
- 7z 启用常见 LZMA/LZMA2 解码，RAR 兼容性受 libarchive 限制；不承诺支持所有变种。
  输出字节预算不等于解码器内存上限，固实压缩/超大字典仍可能产生较高内存压力，需真机压力验证。
- 单独 TXT 通过 Android 选择器导入时没有兄弟图片权限，可能成为缺图工程；目前请打包 TXT 和所需图源再导入。
- ZIP 使用 UTF-8 文件名，拒绝含糊的旧编码、路径逃逸、链接、大小写/规范化冲突和超预算内容。
- 预览限制每页最多 2,000 个、每个工程快照最多 100,000 个标签；正文、校对前后稿和旧版基线分别检查，
  包括存储的已删除标签。超限明确拒绝，不截断；避免导入后标记视图创建海量对象。
- 导出要求正文和校对快照引用的图片均在工作区内且存在；缺图会阻止导出，不生成假装完整的包。
- 图像使用有界缩略解码，并非大图分块引擎；部分编码仍需全尺寸解码，超限图片会加载失败。
- 自动保存只保证已经成功提交的工程；尚无持久编辑草稿和进程崩溃恢复 UI。强杀进程可能丢失最近尚未保存的输入。
- 工作区与分享产物在应用私有目录。卸载/清除应用数据会删除它们；重要内容必须另行导出备份。
- 接收和分享已实现标准 Android 协议，但真实 QQ、厂商 ROM、中文 IME、旋转/低内存验收尚未完成。
  “打开分享面板”不代表发送成功，更不代表对方已经收到。

## 存储与清理

工作区是用户数据，不自动过期；重复导入会产生独立副本，可从工程列表明确删除多余副本。
删除当前工程前提交编辑并保存，成功移出工作区后关闭会话、停止自动保存并清空撤销，再在后台删除文件。
失败时显示错误；已确认删除但未完成的目录在下次清理时继续处理。

- 正常导入/导出的暂存目录在任务结束时释放；异常退出遗留的 `.staging-import-*` / `.staging-export-*`
  仅在超过 24 小时后清理。普通工程、备份和未解决文件事务材料不属于缓存清理范围。
- 系统接收 `inbox` 原有最多 3 个文件、合计 2 GiB 限制；导入尝试后释放接收副本，启动时清理未完成的 `.part`。
- 已完成分享包保存在 `outgoing/<随机 ID>/<分享文件名>.zip`，超过 7 天后可清理；旧版平铺的随机名 ZIP 同样处理。
  启动、运行中每小时及导出前尝试清理，和导入/导出串行，不在分享面板返回时立即删除。
- 分享缓存总上限 2 GiB，超过时拒绝发布新包，不为腾空间删除尚未过期的分享包或工程。
  七天是保留策略，不代表接收方已经读取；跨多日未完成发送时需要重新导出。
- 当前移动端不启用桌面的周期自动备份，不会每次自动保存都生成一份永久副本。
  共用保存事务仍保留一份 `.labelqt-previous` 上一版本，后续保存覆盖它，不无限累积。
  清除应用数据或卸载会同时删除本地工程，不能将其当作清理缓存的日常手段。

## 工具链

本次采用：Qt 6.10.2（`android_arm64_v8a`、Qt Image Formats 插件及同版本 `gcc_64` 主机工具）、NDK 27.2.12479018（r27c）、
Android SDK Platform 36 / Build Tools 36.0.0、JDK 17、CMake >= 3.24、Ninja。
Gradle 8.14.3、Android Gradle Plugin 8.10.1、AndroidX Core 1.16.0 来自该 Qt 的官方 Android 模板。
最低 API 28（Android 9），只构建 ARM64。构建 SDK 与 NDK 放在本机即可，不提交到仓库。

Qt 可用官方安装器或 `aqtinstall` 安装；SDK/NDK 用 Android Studio SDK Manager 或官方 `sdkmanager`。
首次配置需下载固定版本 libarchive 3.8.11 与 XZ/liblzma 5.8.4；两者均固定 SHA256。
仅构建 liblzma 库（0BSD），不打包 XZ 命令行工具或 GPL 脚本。首次打包需联网下载 Gradle/Maven 依赖。
Qt / NDK / JDK 版本不能随意混搭，更换后必须重新验证 `std::expected`、JNI、打包和原生库对齐。

```bash
git submodule update --init --recursive
export QT_ANDROID_DIR=/path/to/Qt/6.10.2/android_arm64_v8a
export QT_HOST_PATH=/path/to/Qt/6.10.2/gcc_64
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export JAVA_HOME=/path/to/jdk-17
export PATH="$JAVA_HOME/bin:$PATH"

cmake --preset android-arm64-debug
cmake --build --preset android-arm64-debug --parallel 8
```

输出：`build/android/arm64-debug/src/mobile/android-build/LabelQtMobile.apk`。
该目标使用调试签名，仅用于开发测试，不是正式发行签名。正式签名密钥不得放进仓库。
应用 ID 为 `org.labelqt.mobile`。安装测试：

```bash
"$ANDROID_SDK_ROOT/platform-tools/adb" install -r build/android/arm64-debug/src/mobile/android-build/LabelQtMobile.apk
```

本机本次下载的工具位于 `build/android-tools/`，因此可将上述路径分别设为：
`build/android-tools/qt/6.10.2/android_arm64_v8a`、`build/android-tools/qt/6.10.2/gcc_64`、
`build/android-tools/sdk`、`build/android-tools/jdk-17.0.21+9` 的绝对路径。
这些是被忽略的本机缓存，并非其他开发者必须采用的目录。

应用启动时会创建并解析系统提供的私有数据根目录，统一使用真实路径。
Android 的 `/data/user/0` 等系统目录可能经过符号链接；这不属于用户文件授权失败。
仅可信的系统根目录在启动边界做此处理，不能对任意导入文件调用路径解析来绕过链接检查。
ZIP 条目、工程图片引用和工作区内部的链接仍被拒绝。若旧测试包出现
“传输路径中不允许包含链接：/data/user/0/org.labelqt.mobile/files”，请覆盖安装修正后的 APK；
无需申请全部文件访问权限，也不必卸载或清除现有数据。

## 开发验证

安装主机 Qt >= 6.8 的 Quick / QuickControls2 / Svg / LinguistTools / Test 和 libarchive 开发包：

```bash
cmake --preset mobile-host-debug
cmake --build --preset mobile-host-debug --parallel 8
ctest --preset mobile-host-debug
python scripts/check_translations.py
python scripts/devtool.py module-map android
```

主机可执行文件：`build/mobile-host/src/mobile/LabelQtMobile`。可传一个本地 ZIP/TXT 路径触发导入；
这是便于 UI 开发的主机前端，不替代桌面 Widgets 发行版。主机 TXT 导入可复制明确选择文件的相邻图片，
Android 则只复制得到授权的内容，不推测 URI 的真实磁盘路径。

移动测试包含归档攻击/预算/失败原子性、工作区改名/删除及模型稳定更新、缓存清理范围/配额、
自定义分享名称、稳定选择与撤销、校对会话过期、QML 编辑与小屏/宽屏布局。
`LABELQT_SCREENSHOT_DIR=/absolute/path` 可让 `MobileShellTests` 保存布局截图。
桌面回归必须另跑 `cmake --build --preset linux-debug` 与 `ctest --preset linux-debug`。
遇到本机 ccache 权限问题，可按根目录约定加 `cmake -E env CCACHE_DISABLE=1`。

### 本次验证记录（2026-11-11）

- Android ARM64 调试 APK 已实际交叉编译、链接和打包；不是仅配置通过。
- 移动端 5 组测试、桌面端 13 组回归测试全部通过；快速检查与四语言翻译检查通过。
- APK 调试签名验证通过，原生库 ELF LOAD 段和 APK ZIP 条目通过 16 KiB 对齐检查。
- 最终 Manifest 不申请网络、广泛存储或全部文件访问权限；导入与分享使用 URI 授权。
- QML 在主机离屏环境检查小屏、宽屏、编辑、手势和图像解码上限；这不等同于 Android 真机验收。
- 本机试用产物整理在 `dist/android-preview/`，包含 APK、校验和、说明与已收集的依赖材料。
  该目录被 Git 忽略，不应提交 SDK、调试签名密钥或二进制依赖。

已有 vivo/iQOO 真机人工试用反馈，但尚未完成系统性的真机验收，不能据此保证
QQ 各版本、厂商 ROM、软键盘和后台回收行为。
公开发行前仍须完成下方发布门槛。

## 试用顺序

### 多格式导入验证（2026-11-12）

已验证 ZIP/7z/tar/tar.gz 原图新建、既有 TXT 优先、自然排序、独立身份、导出再导入，
以及路径越界、碰撞、链接、损坏输入、分卷头和输出预算拒绝。
公开 RAR4/RAR5 小样本纳入自动测试；一份扩展名为 ZIP、实际为 RAR5 的本地用户包成功导入 18 页并重新导出。
用户包不提交仓库，可通过 `LABELQT_TEST_IMPORT_ARCHIVE=/absolute/path` 运行可选回归。
追加回归覆盖 macOS 资源文件不误识别、此类路径仍拒绝越界，以及 gzip CRC/长度损坏、截断、
非零尾随数据、拼接流拒绝和零填充兼容。损坏导入不得发布工作区或留下正常结束任务的暂存目录。
上述内容识别验证在主机完成；Android APK 已构建，仍需在真机验证新格式、输入法与 QQ 交接。

### 操作步骤

详细操作、自动保存与本地副本的区别见 [Android 使用说明](android-user-guide.md)。

1. 准备支持的压缩包，包含一个 TXT 和所引用图片，或只包含用于新建工程的原图。
2. 点击“导入压缩包或 TXT”，或在支持标准文件 Intent 的应用里选择用 LabelQt 打开；导入后从本机列表打开。
3. “浏览”模式平移/缩放并点选标记；“添加”模式点图像增加标签；“移动”模式拖动已选标记。
4. 在底部修改文本和分组，用顶部撤销/重做；“标签”页提供文本列表，菜单可筛选分组。
5. “校对”页开始轮次，编辑正文后查看差异、添加页面或标签批注，完成后结束轮次。
6. 菜单“分享 ZIP”确认或修改文件名，确定后保存本机副本，再从固定快照生成 ZIP 并打开分享面板；选择 QQ 或其他接收应用。
7. 接收方下载后通过文件管理器导入。若 QQ 没有把 LabelQt 列为打开方式，先保存文件再用 LabelQt 的系统选择器导入。

## 发布前门槛

必须补齐完整功能、恢复草稿、真机交互和资源压力测试，检查 APK 中每个 `.so` 的 16 KiB ELF 对齐与 ZIP 对齐，
验证系统返回、键盘预编辑、旋转、任务切换、杀进程、URI 撤权和 QQ 冷热启动，不可只检查 APK 文件存在。
依据：[Qt Android 部署](https://doc.qt.io/qt-6.10/deployment-android.html)、
[Android 16 KiB 页](https://developer.android.com/guide/practices/page-sizes)。

Qt 采用动态库，新增 Qml/Quick/QuickControls2/Dialogs 不引入 Qt Virtual Keyboard 等 GPL-only 模块。
公开分发前须按实际 APK 内容收集 Qt 及其内部第三方、AndroidX、libc++、libarchive、liblzma、diff-match-patch 的许可和版本，
提供对应源码获取/重建/自行签名说明；不能把“开源项目”当作不用履行依赖许可证的理由。
见 [第三方声明](../THIRD_PARTY_NOTICES.md) 和 [Qt 许可说明](https://doc.qt.io/qt-6.10/licensing.html)。
