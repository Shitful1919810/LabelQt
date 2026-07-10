#include "ui/canvas/CanvasLabelItems.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QStringList>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <utility>

namespace {
constexpr int markerType = QGraphicsItem::UserType + 100;

QString newlineMarkerHtml()
{
    return QStringLiteral(
        "<span style=\"display:inline-block;margin-left:2px;padding:0 3px;border-radius:3px;"
        "border:1px solid rgba(148,163,184,.85);color:#e5e7eb;background:rgba(30,41,59,.72);"
        "font-size:85%;line-height:1.15;text-decoration:none;\">↵</span>");
}

QStringList softWrappedHtmlLines(const QString& line, int maxLineCharacters)
{
    const int wrapThreshold = std::max(1, maxLineCharacters);
    if (line.isEmpty()) {
        return {QStringLiteral("&nbsp;")};
    }

    QStringList wrappedLines;
    QString currentLine;
    int currentLength = 0;
    qsizetype clusterStart = 0;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, line);
    for (qsizetype clusterEnd = finder.toNextBoundary(); clusterEnd != -1; clusterEnd = finder.toNextBoundary()) {
        currentLine += line.sliced(clusterStart, clusterEnd - clusterStart);
        clusterStart = clusterEnd;
        ++currentLength;
        if (currentLength >= wrapThreshold) {
            wrappedLines.append(currentLine.toHtmlEscaped());
            currentLine.clear();
            currentLength = 0;
        }
    }
    if (!currentLine.isEmpty()) {
        wrappedLines.append(currentLine.toHtmlEscaped());
    }
    return wrappedLines;
}

QString htmlEscapedWithSoftWrapAndNewlineMarkers(QString text, int maxLineCharacters)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = text.split(QLatin1Char('\n'));
    QString html;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (!html.isEmpty()) {
            html += QStringLiteral("<br/>");
        }

        html += softWrappedHtmlLines(lines.at(i), maxLineCharacters).join(QStringLiteral("<br/>"));
        if (i + 1 < lines.size()) {
            html += newlineMarkerHtml();
        }
    }
    return html;
}
} // namespace

int canvasLabelMarkerItemType()
{
    return markerType;
}

QString canvasLabelBubbleHtml(int displayNumber, const QString& text, const QColor& color, int maxLineCharacters)
{
    return QStringLiteral("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"border-collapse:collapse;\">"
                          "<tr>"
                          "<td valign=\"top\" style=\"white-space:nowrap;padding-right:6px;"
                          "color:%1;font-weight:600;\">#%2:</td>"
                          "<td valign=\"top\">%3</td>"
                          "</tr>"
                          "</table>")
        .arg(color.name(), QString::number(displayNumber),
             htmlEscapedWithSoftWrapAndNewlineMarkers(text, maxLineCharacters));
}

CanvasLabelMarkerItem::CanvasLabelMarkerItem(int labelIndex, int displayNumber, bool selected,
                                             labelqt::core::LabelGroupStyle style, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_labelIndex(labelIndex), m_displayNumber(displayNumber), m_selected(selected),
      m_style(std::move(style))
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations);
    setZValue(10.0);
}

int CanvasLabelMarkerItem::type() const
{
    return markerType;
}

int CanvasLabelMarkerItem::labelIndex() const noexcept
{
    return m_labelIndex;
}

QRectF CanvasLabelMarkerItem::boundingRect() const
{
    const double radius = m_style.markerDiameter / 2.0;
    return QRectF(-radius, -radius, m_style.markerDiameter, m_style.markerDiameter);
}

void CanvasLabelMarkerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(m_selected ? QColor(46, 103, 230) : Qt::white, m_selected ? 3.0 : 1.5));
    painter->setBrush(m_style.groupColor.isValid() ? m_style.groupColor : Qt::black);
    const QRectF shapeRect = boundingRect().adjusted(1.0, 1.0, -1.0, -1.0);
    if (m_style.markerShape == labelqt::core::MarkerShape::Square) {
        painter->drawRect(shapeRect);
    }
    else {
        painter->drawEllipse(shapeRect);
    }

    painter->setPen(Qt::white);
    QFont font = painter->font();
    font.setPointSizeF(m_style.fontPointSize);
    font.setBold(true);
    painter->setFont(font);
    const QString number = QString::number(m_displayNumber);
    painter->drawText(boundingRect(), Qt::AlignCenter, number);
}

CanvasLabelTextBubbleItem::CanvasLabelTextBubbleItem(int displayNumber, QString text,
                                                     labelqt::core::LabelGroupStyle style, QFont bubbleFont,
                                                     double opacity, int maxLineCharacters, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_displayNumber(displayNumber), m_text(std::move(text)), m_style(std::move(style)),
      m_bubbleFont(std::move(bubbleFont)), m_opacity(opacity), m_maxLineCharacters(std::max(1, maxLineCharacters))
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations);
    setZValue(11.0);
    rebuildDocument();
}

QRectF CanvasLabelTextBubbleItem::boundingRect() const
{
    const QSizeF textSize = m_document.size();
    return QRectF(m_style.markerDiameter / 2.0 + 8.0, -textSize.height() / 2.0 - 6.0, textSize.width() + 16.0,
                  textSize.height() + 12.0);
}

void CanvasLabelTextBubbleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(m_opacity);
    const QPalette palette = QApplication::palette();
    painter->setPen(QPen(palette.color(QPalette::Mid), 1.0));
    painter->setBrush(palette.color(QPalette::ToolTipBase));
    painter->drawRoundedRect(boundingRect(), 3.0, 3.0);

    painter->save();
    painter->translate(boundingRect().topLeft() + QPointF(8.0, 6.0));
    m_document.drawContents(painter);
    painter->restore();
}

void CanvasLabelTextBubbleItem::rebuildDocument()
{
    const QColor color = m_style.groupColor.isValid() ? m_style.groupColor : QColor(Qt::black);
    m_document.setDefaultFont(m_bubbleFont);
    m_document.setDocumentMargin(0.0);
    m_document.setHtml(canvasLabelBubbleHtml(m_displayNumber, m_text, color, m_maxLineCharacters));
}
