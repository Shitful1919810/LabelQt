#include "services/TextDiffService.h"

#ifdef DELETE
#undef DELETE
#endif

#include "diff_match_patch.h"

namespace labelqt::services {
namespace {

QVector<TextDiffChunk> fallbackDiff(const QString& beforeText, const QString& afterText)
{
    QVector<TextDiffChunk> chunks;
    if (!beforeText.isEmpty()) {
        chunks.append({TextDiffOperation::Delete, beforeText});
    }
    if (!afterText.isEmpty()) {
        chunks.append({TextDiffOperation::Insert, afterText});
    }
    return chunks;
}

QVector<TextDiffChunk> chunksFromDiffs(const QList<Diff>& diffs)
{
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

} // namespace

QVector<TextDiffChunk> TextDiffService::diff(const QString& beforeText, const QString& afterText)
{
    if (beforeText == afterText) {
        return {{TextDiffOperation::Equal, beforeText}};
    }

    diff_match_patch differ;
    try {
        QList<Diff> diffs = differ.diff_main(beforeText, afterText);
        differ.diff_cleanupSemantic(diffs);
        differ.diff_cleanupSemanticLossless(diffs);
        return chunksFromDiffs(diffs);
    } catch (...) {
        return fallbackDiff(beforeText, afterText);
    }
}

} // namespace labelqt::services
