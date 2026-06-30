#pragma once

#include "core/TextDiffCleanupMode.h"

#include <QString>
#include <QVector>

namespace labelqt::services {

enum class TextDiffOperation {
    Equal,
    Insert,
    Delete,
};

struct TextDiffChunk {
    TextDiffOperation operation{TextDiffOperation::Equal};
    QString text;
};

class TextDiffService final {
public:
    static void setCleanupMode(labelqt::core::TextDiffCleanupMode mode) noexcept;
    static QVector<TextDiffChunk> diff(const QString& beforeText, const QString& afterText);
};

} // namespace labelqt::services
