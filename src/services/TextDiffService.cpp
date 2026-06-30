#include "services/TextDiffService.h"

#ifdef DELETE
#undef DELETE
#endif

#include "diff_match_patch.h"

#include <atomic>

namespace labelqt::services {
namespace {

std::atomic<labelqt::core::TextDiffCleanupMode> g_cleanupMode{labelqt::core::TextDiffCleanupMode::Auto};

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

bool chunksPreserveInputs(const QVector<TextDiffChunk>& chunks, const QString& beforeText, const QString& afterText)
{
    QString reconstructedBefore;
    QString reconstructedAfter;
    for (const TextDiffChunk& chunk : chunks) {
        switch (chunk.operation) {
        case TextDiffOperation::Equal:
            reconstructedBefore += chunk.text;
            reconstructedAfter += chunk.text;
            break;
        case TextDiffOperation::Delete:
            reconstructedBefore += chunk.text;
            break;
        case TextDiffOperation::Insert:
            reconstructedAfter += chunk.text;
            break;
        }
    }
    return reconstructedBefore == beforeText && reconstructedAfter == afterText;
}

bool isCjkCharacter(QChar ch)
{
    switch (ch.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
    case QChar::Script_Bopomofo:
        return true;
    default:
        return false;
    }
}

bool shouldUseSemanticCleanup(const QString& beforeText, const QString& afterText)
{
    const labelqt::core::TextDiffCleanupMode mode = g_cleanupMode.load(std::memory_order_relaxed);
    if (mode == labelqt::core::TextDiffCleanupMode::Semantic) {
        return true;
    }
    if (mode == labelqt::core::TextDiffCleanupMode::Raw) {
        return false;
    }

    qsizetype cjkCount = 0;
    qsizetype textCount = 0;
    const QString combined = beforeText + afterText;
    for (const QChar ch : combined) {
        if (ch.isSpace()) {
            continue;
        }
        ++textCount;
        if (isCjkCharacter(ch)) {
            ++cjkCount;
        }
    }

    if (textCount == 0) {
        return false;
    }
    return static_cast<double>(cjkCount) / static_cast<double>(textCount) < 0.3;
}

} // namespace

void TextDiffService::setCleanupMode(labelqt::core::TextDiffCleanupMode mode) noexcept
{
    g_cleanupMode.store(mode, std::memory_order_relaxed);
}

QVector<TextDiffChunk> TextDiffService::diff(const QString& beforeText, const QString& afterText)
{
    if (beforeText == afterText) {
        return {{TextDiffOperation::Equal, beforeText}};
    }

    diff_match_patch differ;
    try {
        QList<Diff> diffs = differ.diff_main(beforeText, afterText);
        const QVector<TextDiffChunk> rawChunks = chunksFromDiffs(diffs);
        if (!chunksPreserveInputs(rawChunks, beforeText, afterText)) {
            return fallbackDiff(beforeText, afterText);
        }

        if (shouldUseSemanticCleanup(beforeText, afterText)) {
            differ.diff_cleanupSemantic(diffs);
            const QVector<TextDiffChunk> semanticChunks = chunksFromDiffs(diffs);
            if (chunksPreserveInputs(semanticChunks, beforeText, afterText)) {
                return semanticChunks;
            }
        }
        return rawChunks;
    } catch (...) {
    }
    return fallbackDiff(beforeText, afterText);
}

} // namespace labelqt::services
