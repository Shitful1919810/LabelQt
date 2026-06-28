#include "services/TextDiffHtmlRenderer.h"

#include "services/TextDiffService.h"

namespace labelqt::services {
namespace {

QString newlineMarkerHtml(const QString& style)
{
    return QStringLiteral(
               "<span style=\"display:inline-block;border-radius:3px;padding:0 3px;margin:0 2px;"
               "font-size:85%;line-height:1.15;text-decoration:none;%1\">↵</span><br/>")
        .arg(style);
}

QString styledSpan(const QString& text, const QString& style)
{
    if (text.isEmpty()) {
        return {};
    }
    if (style.isEmpty()) {
        return text.toHtmlEscaped();
    }
    return QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text.toHtmlEscaped());
}

QString escapedDiffText(const QString& text, const QString& textStyle, const QString& newlineStyle)
{
    QString html;
    QString segment;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\n')) {
            html += styledSpan(segment, textStyle);
            segment.clear();
            html += newlineMarkerHtml(newlineStyle);
            continue;
        }
        segment.append(ch);
    }
    html += styledSpan(segment, textStyle);
    return html;
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
            html += escapedDiffText(chunk.text, {}, QStringLiteral("border:1px solid rgba(148,163,184,.85);"
                                                                   "color:#e5e7eb;background:rgba(15,23,42,.65);"));
            break;
        case TextDiffOperation::Delete:
            html += escapedDiffText(chunk.text,
                                    QStringLiteral("background:#7f1d1d;color:#ffffff;"
                                                   "text-decoration:line-through;"),
                                    QStringLiteral("border:1px solid #fca5a5;color:#ffffff;background:#7f1d1d;"));
            break;
        case TextDiffOperation::Insert:
            html += escapedDiffText(chunk.text, QStringLiteral("background:#14532d;color:#ffffff;"),
                                    QStringLiteral("border:1px solid #86efac;color:#ffffff;background:#14532d;"));
            break;
        }
    }
    html += QStringLiteral("</p>");
    return html;
}

} // namespace labelqt::services
