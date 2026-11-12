#include "services/proofread/TextDiffHtmlRenderer.h"

#include "services/proofread/TextDiffService.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <array>

namespace labelqt::services {
namespace {

QString newlineImage(const QColor& background)
{
    // Inline PNG works in Quick/Widgets rich text, browsers and PDF without a symbol font or external resource.
    QImage image(56, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(4, 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0, 0, 14, 12), 2, 2);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath arrow;
    arrow.moveTo(10.5, 2.5);
    arrow.lineTo(10.5, 7);
    arrow.lineTo(3.5, 7);
    arrow.moveTo(5.5, 4.5);
    arrow.lineTo(3, 7);
    arrow.lineTo(5.5, 9.5);
    painter.drawPath(arrow);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("<img alt=\"[LF]\" width=\"14\" height=\"12\" src=\"data:image/png;base64,%1\"/><br/>")
        .arg(QString::fromLatin1(png.toBase64()));
}

QString newlineMarkerHtml(TextDiffOperation operation)
{
    // Immutable function-local initialization is safe for concurrent report workers.
    static const std::array markers{newlineImage(QColor("#475569")), newlineImage(QColor("#14532d")),
                                    newlineImage(QColor("#7f1d1d"))};
    return markers.at(static_cast<size_t>(operation));
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

QString escapedDiffText(const QString& text, const QString& textStyle, TextDiffOperation operation)
{
    QString html;
    QString segment;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\n')) {
            html += styledSpan(segment, textStyle);
            segment.clear();
            html += newlineMarkerHtml(operation);
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
                                               const QString& noTextChangeText, bool showUnchangedText)
{
    if (beforeText == afterText && !showUnchangedText) {
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
            html += escapedDiffText(chunk.text, {}, chunk.operation);
            break;
        case TextDiffOperation::Delete:
            html += escapedDiffText(chunk.text,
                                    QStringLiteral("background:#7f1d1d;color:#ffffff;"
                                                   "text-decoration:line-through;"),
                                    chunk.operation);
            break;
        case TextDiffOperation::Insert:
            html += escapedDiffText(chunk.text, QStringLiteral("background:#14532d;color:#ffffff;"), chunk.operation);
            break;
        }
    }
    html += QStringLiteral("</p>");
    return html;
}

} // namespace labelqt::services
