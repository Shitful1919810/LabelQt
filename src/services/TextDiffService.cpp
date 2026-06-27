#include "services/TextDiffService.h"

#ifdef DELETE
#undef DELETE
#endif

#include "diff_match_patch.h"

namespace labelqt::services {

QVector<TextDiffChunk> TextDiffService::diff(const QString& beforeText, const QString& afterText)
{
    diff_match_patch differ;
    QList<Diff> diffs = differ.diff_main(beforeText, afterText);
    differ.diff_cleanupSemantic(diffs);
    differ.diff_cleanupSemanticLossless(diffs);

    QVector<TextDiffChunk> chunks;
    chunks.reserve(diffs.size());
    for (const Diff& diff : std::as_const(diffs)) {
        TextDiffOperation operation{TextDiffOperation::Equal};
        if (diff.operation == ::INSERT) {
            operation = TextDiffOperation::Insert;
        } else if (diff.operation == ::DELETE) {
            operation = TextDiffOperation::Delete;
        }
        chunks.append({operation, diff.text});
    }
    return chunks;
}

} // namespace labelqt::services
