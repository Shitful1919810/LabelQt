#include "ui/CanvasLabelItems.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QStringList>

#include <utility>

namespace {
constexpr int markerType = QGraphicsItem::UserType + 100;

QString htmlEscapedWithLineBreaks(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList escapedLines;
    const QStringList lines = text.split(QLatin1Char('\n'));
    escapedLines.reserve(lines.size());
    for (const QString& line : lines) {
        escapedLines.append(line.toHtmlEscaped());
    }
    return escapedLines.join(QStringLiteral("<br/>"));
}
} // namespace

int canvasLabelMarkerItemType()
{
    return markerType;
}

QString canvasLabelBubbleHtml(int displayNumber, const QString& text, const QColor& color)
{
    return QStringLiteral("<span style=\"color:%1; font-weight:600;\">#%2</span> : %3")
        .arg(color.name(), QString::number(displayNumber), htmlEscapedWithLineBreaks(text));
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
                                                     double opacity, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_displayNumber(displayNumber), m_text(std::move(text)), m_style(std::move(style)),
      m_bubbleFont(std::move(bubbleFont)), m_opacity(opacity)
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
    m_document.setHtml(canvasLabelBubbleHtml(m_displayNumber, m_text, color));
}
