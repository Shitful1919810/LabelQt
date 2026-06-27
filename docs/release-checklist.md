# 发布检查清单

这份清单用于发布 LabelQt 的预览版或正式版。目标是让每次发布都能复用同一套步骤，减少遗漏运行时文件、许可证文件或测试验证的风险。

## 发布前确认

- 确认 `main` 分支干净，目标提交已经通过必要审查。
- 确认版本号、release tag 和 release 标题一致，例如：
  - tag：`v0.1.0-alpha.2`
  - 标题：`LabelQt v0.1.0-alpha.2`
- 更新并检查 release note。
- 确认 `README.md`、`THIRD_PARTY_NOTICES.md` 和 `docs/porting-dependencies.md` 没有与当前构建方式矛盾。

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
- `licenses/` 下的第三方许可证文本

不应包含：

- `LabelQtTests.exe`
- `Qt6Test.dll`
- `.pdb`、`.ilk`、`.exp`、`.lib` 等开发产物
- CMake 构建缓存
- 用户本地脚本配置、API key、日志或模型缓存

打包后在一个干净目录里解压 zip，并从解压目录直接运行 `labelqt.exe`。不要只测试构建目录中的 exe。

可选：在 Windows 上安装 WiX Toolset v4+ 及其 UI 扩展后，可以基于同一个已部署的 Release 目录生成带标准安装向导的 MSI：

```powershell
python scripts/package_windows_msi.py `
  --source path\to\Release `
  --version v0.1.0-alpha.2 `
  --output-dir dist
```

MSI 会显示“下一步、安装路径、完成”等标准界面，把程序安装到 `Program Files\LabelQt`，并携带与 zip 相同的运行时文件、默认 `preference.json` 和官方脚本。发布 MSI 前应额外在干净 Windows 环境中测试安装、启动、卸载和升级流程。

## Linux 原生包

Linux release 构建：

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

生成本机安装包：

```bash
cmake --build --preset linux-release --target package
```

也可以单独生成某种格式：

```bash
cpack --config build/linux/release/CPackConfig.cmake -G DEB
cpack --config build/linux/release/CPackConfig.cmake -G RPM
```

安装包应遵循 Linux 文件布局：

- 可执行文件：`/usr/bin/labelqt`
- 文档和许可证：`/usr/share/doc/labelqt`
- 第三方许可证文本：`/usr/share/doc/labelqt/licenses`
- 桌面入口：`/usr/share/applications/org.labelqt.LabelQt.desktop`
- 图标：`/usr/share/icons/hicolor/scalable/apps/org.labelqt.LabelQt.svg`

Linux 包不捆绑默认 `preference.json`、Python、OCR 模型或自动化脚本依赖；官方自动化脚本应安装到 `/usr/share/labelqt/scripts/official`。偏好设置默认值由程序内置提供；用户脚本应放在程序显示的用户脚本目录中。Windows 便携包可以携带 exe 旁边的 `preference.json`、`scripts/official` 和 `scripts/custom`，用于提供默认体验与便携脚本目录。

## 发布到 GitHub

- 在对应提交上创建 tag。
- 上传 Windows zip。
- 粘贴 release note。
- 标记 alpha/beta/pre-release 状态。
- 发布后下载 GitHub 页面上的附件，再做一次解压运行检查。
