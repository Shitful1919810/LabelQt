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

bool shouldRenderAsReplacement(const QVector<TextDiffChunk>& chunks)
{
    int firstChangeIndex = -1;
    int lastChangeIndex = -1;
    qsizetype insertLength = 0;
    qsizetype deleteLength = 0;
    qsizetype equalLength = 0;
    bool hasInsert = false;
    bool hasDelete = false;
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks.at(i).text.isEmpty()) {
            continue;
        }
        if (chunks.at(i).operation == TextDiffOperation::Equal) {
            equalLength += chunks.at(i).text.simplified().size();
            continue;
        }
        hasInsert = hasInsert || chunks.at(i).operation == TextDiffOperation::Insert;
        hasDelete = hasDelete || chunks.at(i).operation == TextDiffOperation::Delete;
        insertLength += chunks.at(i).operation == TextDiffOperation::Insert ? chunks.at(i).text.simplified().size() : 0;
        deleteLength += chunks.at(i).operation == TextDiffOperation::Delete ? chunks.at(i).text.simplified().size() : 0;
        if (firstChangeIndex < 0) {
            firstChangeIndex = i;
        }
        lastChangeIndex = i;
    }
    if (!hasInsert || !hasDelete || firstChangeIndex < 0 || lastChangeIndex <= firstChangeIndex) {
        return false;
    }
    const qsizetype changedLength = insertLength + deleteLength;
    if (changedLength >= 8 && changedLength >= equalLength * 2) {
        return true;
    }
    if (changedLength < equalLength * 2) {
        return false;
    }
    if (chunks.at(firstChangeIndex).operation == chunks.at(lastChangeIndex).operation) {
        return false;
    }

    for (int i = firstChangeIndex + 1; i < lastChangeIndex; ++i) {
        if (chunks.at(i).operation == TextDiffOperation::Equal && chunks.at(i).text.simplified().size() >= 2) {
            return true;
        }
    }
    return false;
}

QString renderReplacementDiff(const QString& beforeText, const QString& afterText)
{
    return QStringLiteral("<p style=\"line-height:1.55;margin-bottom:0.35em\">%1</p>"
                          "<p style=\"line-height:1.55;margin-top:0\">%2</p>")
        .arg(escapedDiffText(beforeText,
                             QStringLiteral("background:#7f1d1d;color:#ffffff;text-decoration:line-through;"),
                             QStringLiteral("border:1px solid #fca5a5;color:#ffffff;background:#7f1d1d;")),
             escapedDiffText(afterText, QStringLiteral("background:#14532d;color:#ffffff;"),
                             QStringLiteral("border:1px solid #86efac;color:#ffffff;background:#14532d;")));
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
    if (shouldRenderAsReplacement(chunks)) {
        return renderReplacementDiff(beforeText, afterText);
    }

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
