#pragma once

#include <QString>

namespace labelqt::services {

class TextDiffHtmlRenderer final {
public:
    static QString renderInlineDiff(const QString& beforeText, const QString& afterText,
                                    const QString& noTextChangeText);
    static QString escapedText(QString text);
};

} // namespace labelqt::services
