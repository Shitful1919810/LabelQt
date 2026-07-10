# Windows 静态版重链接说明

LabelQt 源码采用 MIT 许可证发布。Windows 静态发行包会同时提供构建该版本时使用的 Qt 对应源码地址、
原生依赖许可证和本说明，允许接收者修改 LGPL 覆盖的 Qt 组件并重新构建、链接和运行 LabelQt。

本说明是发行工程流程的一部分，不构成法律意见。每次静态发行仍需根据实际 Qt 与依赖版本复核许可证。

## 准备源码

1. 从该发行版本对应的 Git tag 获取 LabelQt 完整源码，并初始化 `third_party/diff-match-patch` 子模块。
2. 从随包 `STATIC_LINKING_NOTICE.txt` 记录的地址获取该版本使用的 Qt 对应源码。
3. 准备与发行说明一致的 Visual Studio 2022、CMake 和静态依赖。使用 vcpkg 时，应检出发行记录指定的
   vcpkg baseline，并使用 `x64-windows-static` triplet。
4. Qt、QtKeychain 及其传递依赖必须使用与 MSVC `/MT` 兼容的静态构建。

## 配置与构建

以下命令展示使用 vcpkg 静态 triplet 的典型流程，路径需按本机环境调整：

```powershell
cmake --preset windows-vs-static-release `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build --preset windows-vs-static-release
```

静态 preset 会拒绝动态 Qt。生成的程序位于 Visual Studio Release 输出目录，文件名为
`labelqt-static.exe`。Qt 的 Windows 平台插件会静态导入；其他图片格式或平台能力取决于所用静态 Qt
构建中实际提供的插件。

若要验证重链接能力，可以修改 Qt 源码、以相同静态配置重新构建并安装 Qt，然后让
`CMAKE_PREFIX_PATH` 或 `Qt6_DIR` 指向修改后的 Qt，再重新配置和构建 LabelQt。

## 验证

使用 Visual Studio 的依赖工具检查最终文件：

```powershell
dumpbin /dependents path\to\labelqt-static.exe
```

正式静态产物不应依赖 Qt、QtKeychain 或 MSVC C/C++ 运行库 DLL。随后应在未安装 Qt、
vcpkg 和 Visual Studio 的干净 Windows 环境中验证启动、图片读取、主题、翻译、密钥环和自动化脚本。

LabelQt 不对为调试 LGPL 覆盖组件修改而进行的逆向工程施加额外限制，也不使用技术措施阻止运行重新
链接后的程序。
