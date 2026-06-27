# 校对差异算法说明

本文说明 LabelQt 当前校对差异和工程对比功能使用的 diff 逻辑。这个功能服务于校对人员快速查看文本工程发生了哪些变化，不是严格的版本控制或三方合并系统。

## 目标

校对人员通常会像翻译一样直接编辑工程：修改文本、增删 label、调整 label 顺序、移动 marker 或改类别。因此 diff 系统需要回答几个问题：

- 哪些 label 是新增或删除。
- 哪些 label 的文本、类别或 marker 坐标发生变化。
- 哪些 label 只是页内顺序移动。
- 内部“开始校对基线”和外部“与另一个工程对比”应使用同一套判断逻辑。

LabelQt 不再为 label 引入稳定 ID。经典 LabelPlus txt 主体中没有这种身份信息，额外维护 ID 会让复制、粘贴、合并、自动化脚本和外部工具编辑都变复杂。现在的设计把每一页中的 label 列表看作类似文本 diff 中的“行序列”，通过启发式序列对齐推断新旧 label 的对应关系。

## 模块职责

- `ReviewMetadataService`：只负责保存和读取“开始校对”时的基线快照。快照包含页名、当时的 label 序号、文本、类别和 marker 坐标。
- `ProjectComparisonService`：负责按页组织比较，把当前工程和基线快照转换为每页的 label 快照列表，然后把页内匹配交给 `LabelSequenceDiffService`。
- `LabelSequenceDiffService`：只负责一页内的 label 序列对齐，输出旧 label 和新 label 的对应关系，以及新增、删除和移动信息。
- `TextDiffService`：只负责单个已匹配 label 的文本内部差异高亮。它使用 `diff-match-patch`，失败时退化为整段删除/插入，不能把异常抛到 UI 层。
- `ProofreadChangesDialog`：只展示 `ReviewChange`，不参与匹配算法。

这个边界很重要：UI 不应该知道 label 如何匹配，编辑控制器也不应该承担 diff 逻辑。

## 对比流程

无论是内部修订对比还是外部工程对比，最终都走同一条路径：

1. 构造基线快照。
   - 内部校对：从当前工程的 `review` metadata 读取基线。
   - 外部工程对比：临时把用户选择的工程捕获成基线快照。
2. `ProjectComparisonService::changesForProject()` 收集所有涉及的页名。
3. 对每一页分别生成：
   - baseline labels：基线中的非删除 label 快照。
   - current labels：当前工程中的非删除 label 快照。
4. 调用 `LabelSequenceDiffService::diff()` 推断页内对应关系。
5. 把对应关系转换为 `ReviewChange`：
   - 旧无新有：`Added`
   - 旧有新无：`Deleted`
   - 旧有新有：`Modified`
6. 对 `Modified` 再判断：
   - 文本不同：`textChanged`
   - 类别不同：`groupChanged`
   - marker 坐标不同：`positionChanged`
   - 被序列算法判定为移动：`orderChanged`

如果一个匹配项没有任何变化，就不会出现在差异列表中。

## 页内序列对齐算法

`LabelSequenceDiffService` 的输入是同一页的新旧 label 快照列表。算法分三步。

### 1. 保序精确文本对齐

第一阶段类似 LCS / vimdiff 的行序列对齐，但只允许“归一化文本相同”的 label 进入保序对齐。

归一化目前使用 `QString::simplified()`，主要忽略多余空白差异。

这一步的目的不是发现所有修改，而是找到“顺序基本没变、文本相同”的稳定骨架。这样当某个 label 插入或移动时，后面的未修改 label 不会因为序号变化而被全部误报。

### 2. 未匹配项二次配对

第一阶段结束后，仍未匹配的新旧 label 会进入二次配对。配对使用一个启发式分数：

- 文本完全相同：高分。
- 文本相似：按字符 LCS 相似度给分。
- 短文本相似但不相同：强制降低分数，避免“啊”“嗯”“第一句”“第二句”这类短文本被误配。
- 类别相同：少量加分。
- marker 坐标接近：少量加分，只用于消歧。
- 原序号相同：少量加分。
- 原序号距离较远：少量扣分。

分数超过阈值的最佳候选会被配成同一个 label。若旧序号和新序号不同，这个匹配会标记为 `moved`。

这一步用于识别两类情况：

- 文本相同但位置变了：通常是页内顺序移动。
- 文本变化但仍然明显对应：通常是校对修改。

### 3. 剩余项作为新增/删除

二次配对后仍未匹配的旧 label 视为删除，仍未匹配的新 label 视为新增。

## 为什么不使用稳定 ID

稳定 ID 能提供更精确的身份追踪，但会引入额外复杂度：

- 需要在工程 metadata 中保存和恢复每个 label 的 ID。
- 新建、复制、剪切、粘贴、自动化脚本、合并工程和页面顺序调整都要维护 ID 生命周期。
- 外部工具编辑工程时可能丢失 ID，导致逻辑再次降级。

当前校对场景更关注文本质量，diff 作为辅助视图可以接受少量启发式误差。因此 LabelQt 选择不向 LabelPlus 工程主体引入稳定 ID，而是用页内序列对齐降低复杂度。

## 已知边界

这个算法不是完美对象追踪，以下情况可能退化：

- 同一页存在大量重复短文本，例如多个“啊”“嗯”“！”。
- 校对同时大幅改写文本、移动顺序、移动 marker。
- 新旧工程本身完全不相关。
- 外部工具重排页面或重排 label 后，又同时重写大量文本。

退化时，LabelQt 可能把某些变化显示为新增/删除或粗粒度修改。要求是不能崩溃，不能阻塞用户继续查看和编辑。

## 后续调整原则

- 改匹配策略时优先修改 `LabelSequenceDiffService`，不要把匹配条件散落到 UI 或 `ProjectComparisonService`。
- 新增 diff 展示字段时，先扩展 `ReviewChange`，再让对话框展示。
- 单个 label 的文本内部高亮仍由 `TextDiffService` 处理，不要把文本 diff 和 label 序列 diff 混在一起。
- 如果未来需要更强的对象追踪，应重新评估是否引入显式 ID，而不是在多个服务里隐式模拟 ID。
