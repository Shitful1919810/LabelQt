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
- `TextDiffService`：只负责单个已匹配 label 的文本内部差异切分。它使用 `diff-match-patch`，失败时退化为整段删除/插入，不能把异常抛到 UI 层。
- `TextDiffHtmlRenderer`：把 `TextDiffService` 的结构化结果渲染为可嵌入对话框或报告的 HTML 片段。
- `ProofreadReportService`：生成校对 HTML 报告。它只负责文件内容，不弹文件选择窗口，也不读取 UI 状态。报告按页面分组，使用类似校对预览界面的双图展示 marker；内部校对可显示为“修改前 / 修改后”，外部工程对比显示为“当前工程 / 目标工程”。报告始终把压缩后的页面图像以内嵌 base64 的方式写入单个 HTML 文件。同一页两侧图片路径相同时，内嵌图片只写入一次并由两个预览复用。
- `ProofreadChangesDialog`：只展示 `ReviewChange`，不参与匹配算法。

这个边界很重要：UI 不应该知道 label 如何匹配，编辑控制器也不应该承担 diff 逻辑。

## 对比流程

无论是内部修订对比还是外部工程对比，最终都走同一条路径：

1. 构造基线快照。
   - 内部校对：从当前工程的 `review` metadata 读取基线。
   - 外部工程对比：临时把当前打开的工程捕获成基线快照，把用户选择的工程视为目标工程。
2. `ProjectComparisonService` 建立页面匹配。
   - 内部校对直接按当前工程页名匹配。
   - 外部工程对比优先按规范化文件名匹配，例如不同目录下的 `001.png` 视为同一页。
   - 文件名无法匹配的剩余页面，会尝试读取两边图片路径并用低分辨率灰度指纹匹配。指纹只用于建立页对应关系，不参与 label 文本 diff。
   - 如果文件名和图片指纹都无法匹配足够多页面，但两个工程页数相同，UI 会询问是否改为按当前页序一一匹配。
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

页面匹配只决定“哪两页参与 diff”。预览图片路径仍分别来自两个工程各自的 `ImageEntry::path`；也就是说原稿和校对稿放在不同目录时，左右预览会各自读取各自目录下的图源。只有某一侧图片缺失时，预览才会退回使用另一侧可用图片。

## 页面匹配与图片指纹

外部工程对比时，页面匹配由 `ProjectComparisonService` 完成。它不直接比较完整图片，也不会修改两个工程的页序。

当前策略分三层：

1. **规范化文件名匹配**
   - 对 `ImageEntry::name` 取 `QFileInfo(name).fileName()`。
   - 不同目录下的同名文件会视为同一页，例如 `draft/001.png` 与 `proofread/001.png`。
   - 同一工程内如果规范化文件名重复，该名字会视为歧义项，不参与自动同名匹配。

2. **图片指纹匹配**
   - 只处理第一步剩下的未匹配页面。
   - 分别读取 baseline/current 两边 `ImageEntry::path` 指向的图片，因此原稿和校对稿可以在不同目录。
   - 读取失败的图片不参与指纹匹配，后续会退化到新增/删除或按页序 fallback。
   - 指纹生成方式：
     - 使用 `QImageReader` 读取图片并启用 `autoTransform`。
     - 先通过 `QImageReader::size()` 记录原始尺寸，再用 `setScaledSize()` 只解码指纹所需的小缩略图，避免为了匹配页面而完整读入大图。
     - 转成灰度图。
     - 解码为 `32x16` 的低分辨率缩略图。
     - 计算 512 个像素的平均灰度。
     - 每个像素是否大于等于平均值形成一位，最终得到 512-bit average hash。
     - 指纹结果按图片路径、文件大小和修改时间缓存在内存中；同一次比较流程里不会重复读取同一张图片。
   - 自动匹配要求：
     - 两张图原始尺寸相同。
     - 两个 hash 的 Hamming 距离不超过当前阈值。
   - 这个 hash 只用于识别“这两张图是不是同一页”，不参与 label 文本、坐标或类别的差异判定。

3. **按页序 fallback**
   - 如果文件名和图片指纹仍无法匹配足够多页面，且两个工程页数相同，UI 会询问是否按当前页序一一匹配。
   - 这是最后兜底，适合文件名和图片读取都不可用但用户确认两份工程页序一致的场景。

这种顺序能处理常见的三类工程：

- 两份工程在不同目录，但图片文件名一致。
- 图片文件名不同，甚至页序被调整过，但图源本身相同。
- 自动匹配不足时，由用户明确选择是否信任页序。

外部工程对比会先生成一个 `ProjectComparisonPlan`。这个 plan 保存基线快照、页面配对结果和自动匹配数量，后续询问是否按页序 fallback、生成 diff 结果都会复用同一份计划。不要在 UI 层分别调用多个路径重复做页面匹配或图片指纹读取。

## 页内序列对齐算法

`LabelSequenceDiffService` 的输入是同一页的新旧 label 快照列表。算法分三步。

### 1. 保序精确文本对齐

第一阶段类似 LCS / vimdiff 的行序列对齐，但只允许“归一化文本相同”的 label 进入保序对齐。

归一化目前使用 `QString::simplified()`，主要忽略多余空白差异。

这一步的目的不是发现所有修改，而是找到“顺序基本没变、文本相同”的稳定骨架。这样当某个 label 插入或移动时，后面的未修改 label 不会因为序号变化而被全部误报。

### 2. 未匹配项二次配对

第一阶段结束后，仍未匹配的新旧 label 会进入二次配对。配对使用一个启发式分数：

- 文本完全相同：高分。
- 文本相似：按字符 LCS 相似度给分，并按较短文本长度乘以置信系数。文本越短，单个相同字符越可能只是巧合，因此相似度权重越低。
- 类别相同：少量加分。
- marker 坐标接近：使用快速衰减的平滑函数加分。坐标完全重合或只有小幅移动时会给较高权重，即使短文本被大幅改写，也优先视为同一个 label；距离继续拉开后分数会快速下降，避免把只是大致在同一区域的不同 label 误配。
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

- 改页面匹配策略时优先修改 `ProjectComparisonService`，不要把页面身份判断散落到 UI。
- 改页内 label 匹配策略时优先修改 `LabelSequenceDiffService`，不要把 label 匹配条件散落到 UI 或 `ProjectComparisonService`。
- 新增 diff 展示字段时，先扩展 `ReviewChange`，再让对话框展示。
- 单个 label 的文本内部切分仍由 `TextDiffService` 处理，HTML 展示由 `TextDiffHtmlRenderer` 处理，不要把文本 diff、HTML 生成和 label 序列 diff 混在一起。
- `TextDiffHtmlRenderer` 会把换行符渲染成可见的 `↵` 标记再换行，使新增或删除换行在校对窗口和 HTML 报告里都能被看见。
- 校对报告导出逻辑应继续放在 `ProofreadReportService`，对话框只负责收集保存路径和本地化文案。报告图像压缩和内嵌策略属于服务层，不应散落到 UI。
- 如果未来需要更强的对象追踪，应重新评估是否引入显式 ID，而不是在多个服务里隐式模拟 ID。

## 变更筛选与摘要分类

校对变更窗口可以同时按页面、变更类型和摘要项过滤。三个过滤条件取交集；导出 HTML 校对报告时只导出当前过滤后仍可见的变更。

摘要项由 `ReviewChangeClassifier` 统一计算，UI 表格和 `ProofreadReportService` 不应各自重新判断：

- `文本`：文本变更中出现非空白、非标点字符的新增或删除。
- `格式`：文本变更只涉及或同时涉及空白字符、换行、半角/全角标点等格式性字符。
- `类别`：label group 发生变化。
- `标记`：marker 坐标发生变化。
- `顺序`：页内 label 顺序发生变化。

一条变更可以同时拥有多个摘要项，例如“文本、格式、标记”。当用户过滤摘要项时，只要一条变更包含任一选中的摘要项，就应显示。

## diff-match-patch 使用约定

LabelQt 短期继续使用 `diff-match-patch` 处理单个 label 内部的文本差异，但只能通过 `TextDiffService`
这一层访问。当前安全调用链是：

1. `diff_match_patch::diff_main(beforeText, afterText)`
2. 按偏好设置决定是否调用 `diff_match_patch::diff_cleanupSemantic(diffs)`
3. 把 `Diff` 转换成 `TextDiffChunk`
4. 校验这些 chunk 是否能重新拼回原始的修改前文本和修改后文本
5. 如果 cleanup 后校验失败，则退回未经 cleanup 的 raw diff；如果 raw diff 也异常或无法还原，再退化为整段删除和整段新增

不要在 UI、HTML 导出或其他服务里直接 new `diff_match_patch`。这样做会让 diff 粒度、异常处理和
输入还原校验分散到多个地方，后续很容易再次引入崩溃。

`proofreading.textDiffCleanup` 控制文本 diff 的 cleanup 策略：

- `auto`：默认策略。统计修改前后文本中的非空白字符，如果 CJK 字符占比大于等于 30%，使用 raw diff；否则使用 semantic cleanup。这个策略适合中文、日文漫画文本和英文文本混用的场景。
- `semantic`：强制使用 `diff_cleanupSemantic()`。英文等空格分词语言通常更易读，但中文、日文短句可能被合并成过粗的整段替换。
- `raw`：强制只使用 `diff_main()` 原始结果。中文、日文通常更细，但英文可能更碎。

### 已知恶性问题

Qt/C++ 版 `diff-match-patch` 的 `diff_cleanupSemanticLossless()` 不能在本项目中使用。我们在校对报告导出中遇到过稳定复现的堆损坏崩溃：

- 先 diff `登顶所见绝美风景的代价是` → `登顶见到绝美风景的代价是`
- 再 diff 一段包含换行的中文长文本
- 如果中间调用 `diff_cleanupSemanticLossless()`，第一组结果就可能出现无法还原输入的片段，例如把共同部分错切成类似 `登顶` / 删除 `所` / 插入 `见到` / 相等 `到见...`
- 随后继续 diff 时会触发 `malloc(): unaligned tcache chunk detected` 一类堆损坏崩溃

这个问题不是普通展示效果不好，而是内存安全问题。当前没有在上游公开 issue 中找到完全对应的报告；而
`google/diff-match-patch` 官方仓库已经归档，短期不应期待上游修复。除非替换掉当前 Qt/C++ port 并加入覆盖
上述中文文本对的回归测试，否则不要恢复 `diff_cleanupSemanticLossless()`。

注意：这个 Qt/C++ port 的 `diff_cleanupSemantic()` 内部也会调用 `diff_cleanupSemanticLossless()`。因此 LabelQt
只在自动模式判定为非 CJK 文本，或用户显式强制开启时才调用 semantic cleanup，并且调用后必须再次校验输出是否能还原输入。

如果未来需要替换 diff 引擎，应满足这些约束：

- `TextDiffService` 的公开接口保持不变，避免 UI 和报告导出跟具体库耦合。
- 新实现必须保证 diff chunk 能还原修改前和修改后文本。
- 必须保留覆盖中文短句、换行差异、长段落和无关文本的测试。
- 第三方库许可证需要写入 `THIRD_PARTY_NOTICES.md`，并在 CMake 中保持动态/静态构建一致。
