# 测试说明

本目录目前提供一个 Qt Test 测试目标：`LabelQtTests`。测试入口在 `LabelTests.cpp`，由
`tests/CMakeLists.txt` 注册到 CTest。

## 运行方式

推荐使用仓库 preset：

```bash
cmake -E env CCACHE_DISABLE=1 cmake --build --preset linux-debug
cmake -E env CCACHE_DISABLE=1 ctest --preset linux-debug
```

也可以运行完整检查脚本：

```bash
python scripts/check_all.py
```

`check_all.py` 会按当前平台选择默认 debug preset；如果要在 Windows 上使用 VS2022 preset，可以显式指定：

```bash
python scripts/check_all.py windows-vs-debug
```

测试目标已设置 `QT_QPA_PLATFORM=offscreen`，因此基础 Qt Widget 交互测试可以在无图形桌面的环境中运行。

## 当前覆盖范围

- `Label` 基础行为：
  - marker 坐标 clamp。

- `LabelPlusDocument`：
  - 经典 LabelPlus `.txt` 工程读写。
  - comment 区域保留。

- 标签导航与筛选：
  - 跨页切换可见标签。
  - group 筛选对导航结果的影响。
  - group 筛选变化后，当前选择会自动剔除已被过滤隐藏的 label。

- 标签剪贴板与粘贴规划：
  - LabelQt 自定义剪贴板 MIME 数据往返。
  - 剪贴板 JSON 损坏时的错误路径。
  - 粘贴插入位置和 undo/redo。
  - 剪切、粘贴后的 undo/redo 选择状态恢复。
  - 批量移动标签的 undo/redo 和选择恢复。
  - 粘贴坐标边界 clamp。
  - 剪切粘贴保留原坐标。
  - 鼠标锚点粘贴时保持多标签相对布局。

- 偏好设置：
  - 自动化脚本快捷键读取。
  - marker 浮点尺寸读取。
  - 默认 group 样式。
  - preference JSON 损坏时的警告路径。

- 自动化脚本服务：
  - 自动化 output 中 `quiet`、`result` 和 `operations` 字段的正常解析。
  - 自动化 output 中非法 `operations` 字段的错误路径。
  - 自动化 runner 在 Python 命令不可用时返回失败结果，而不是悬挂或崩溃。
  - 自动化操作 `addLabel`、`setLabelText`、`setLabelPosition`、`deleteLabel` 的计划与撤销。

- Qt Model/View：
  - `LabelTableModel` 通过 `QAbstractItemModelTester`，并验证 group 筛选、编辑请求和拖放重排请求。
  - `PageOrderListModel` 通过 `QAbstractItemModelTester`，并验证拖放后页面不丢失。
  - `PageOrderListModel` 删除页面后保留合理的后继选择，且不直接修改工程图片列表。

- 会话状态：
  - 最近页面、缩放、中心点和多选标签状态持久化。

- 工程与页面服务：
  - 保存确认/关闭流程中的“保存、放弃、取消”决策映射。
  - 缺失图片扫描。
  - `ProjectController` expected 风格接口的空目录和跳过备份路径。
  - 工程合并的无冲突页、冲突页和最终页序应用。
  - 合并来源 comment 的区间解析。
  - 页面顺序服务的重排，以及重排后来源信息仍按图片名跟随页面。

- 图像画布交互：
  - `Ctrl+点击 marker` 触发多选点击。
  - `Ctrl+拖动 marker` 移动标签且不误触发画面拖动。
  - 多选状态下 `Ctrl+拖动 marker` 会整体移动当前选择。
  - `Ctrl+拖动` 未选中的 marker 时，会只移动该 marker，不带着原选择移动。

## 新增测试建议

- 新增或修改 `QAbstractItemModel` 时，优先加入 `QAbstractItemModelTester`。
- 修复过的交互类 bug，例如 marker 点击、多选、拖动、表格编辑、页面重排，应尽量补轻量 Qt Widget 测试。
- 新增服务层错误路径时，优先测试 `std::expected` 的失败分支，避免错误被空结果静默吞掉。
- 不建议在单元测试里依赖真实外部 Python 包、OCR 模型、在线 API 或用户本机 keychain；这些应通过更高层的手工或集成测试覆盖。
