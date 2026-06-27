#include "services/TextDiffHtmlRenderer.h"

#include "services/TextDiffService.h"

namespace labelqt::services {
namespace {

QString wrappedSpan(const QString& text, const QString& style)
{
    return QStringLiteral("<span style=\"%1\">%2</span>").arg(style, TextDiffHtmlRenderer::escapedText(text));
}

} // namespace

QString TextDiffHtmlRenderer::escapedText(QString text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString TextDiffHtmlRenderer::renderInlineDiff(const QString& beforeText, const QString& afterText,
                                               const QString& noTextChangeText)
{
    if (beforeText == afterText) {
        return QStringLiteral("<p>%1</p>").arg(escapedText(noTextChangeText));
    }

    const QVector<TextDiffChunk> chunks = TextDiffService::diff(beforeText, afterText);
    QString html = QStringLiteral("<p style=\"line-height:1.45\">");
    for (const TextDiffChunk& chunk : chunks) {
        if (chunk.text.isEmpty()) {
            continue;
        }
        switch (chunk.operation) {
        case TextDiffOperation::Equal:
            html += escapedText(chunk.text);
            break;
        case TextDiffOperation::Delete:
            html += wrappedSpan(chunk.text,
                                QStringLiteral("background:#7f1d1d;color:#ffffff;text-decoration:line-through;"));
            break;
        case TextDiffOperation::Insert:
            html += wrappedSpan(chunk.text, QStringLiteral("background:#14532d;color:#ffffff;"));
            break;
        }
    }
    html += QStringLiteral("</p>");
    return html;
}

} // namespace labelqt::services
