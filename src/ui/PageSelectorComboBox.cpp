#include "ui/PageSelectorComboBox.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QStylePainter>
#include <QStyledItemDelegate>

#include <algorithm>

namespace {
constexpr QLatin1Char pageSeparator{'|'};
constexpr int pageSelectorMinimumTextWidth = 220;
constexpr int pageSelectorMaximumTextWidth = 360;
constexpr int pageBadgeHorizontalPadding = 6;

QString pageDisplayText(int pageIndex, const QString& imageName)
{
    return QStringLiteral("%1%2%3").arg(pageIndex + 1, 3, 10, QLatin1Char('0')).arg(pageSeparator).arg(imageName);
}

QColor blendedColor(const QColor& foreground, const QColor& background, double foregroundRatio)
{
    const double backgroundRatio = 1.0 - foregroundRatio;
    const auto mix = [foregroundRatio, backgroundRatio](double foregroundChannel, double backgroundChannel) {
        return static_cast<float>(foregroundChannel * foregroundRatio + backgroundChannel * backgroundRatio);
    };
    return QColor::fromRgbF(mix(foreground.redF(), background.redF()), mix(foreground.greenF(), background.greenF()),
                            mix(foreground.blueF(), background.blueF()));
}

void drawPageText(QPainter* painter, const QRect& rect, const QString& text, const QPalette& palette, bool enabled,
                  bool selected = false)
{
    const qsizetype separatorIndex = text.indexOf(pageSeparator);
    if (separatorIndex < 0) {
        painter->setPen(palette.color(enabled ? QPalette::Normal : QPalette::Disabled, QPalette::Text));
        painter->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, text);
        return;
    }

    const QString pageNumber = text.left(separatorIndex);
    const QString imageName = text.mid(separatorIndex + 1);
    const QPalette::ColorGroup group = enabled ? QPalette::Normal : QPalette::Disabled;
    const QColor textColor = selected ? palette.color(QPalette::HighlightedText) : palette.color(group, QPalette::Text);
    const QColor baseColor = selected ? palette.color(QPalette::Highlight) : palette.color(group, QPalette::Base);
    const QColor badgeBackground =
        selected ? blendedColor(textColor, baseColor, 0.22) : blendedColor(textColor, baseColor, enabled ? 0.16 : 0.10);
    const QColor badgeBorder = blendedColor(textColor, baseColor, enabled ? 0.42 : 0.24);

    const QFontMetrics metrics(painter->font());
    const int badgeWidth = metrics.horizontalAdvance(pageNumber) + pageBadgeHorizontalPadding * 2;
    QRect badgeRect(rect.left(), rect.center().y() - metrics.height() / 2 - 2, badgeWidth, metrics.height() + 4);
    badgeRect = badgeRect.intersected(rect.adjusted(0, 0, -1, 0));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(badgeBorder);
    painter->setBrush(badgeBackground);
    painter->drawRoundedRect(badgeRect, 4, 4);
    painter->setPen(textColor);
    painter->drawText(badgeRect, Qt::AlignCenter, pageNumber);
    painter->restore();

    QRect textRect = rect;
    textRect.setLeft(badgeRect.right() + metrics.horizontalAdvance(QLatin1Char(' ')) + 4);
    painter->setPen(textColor);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(imageName, Qt::ElideMiddle, textRect.width()));
}

class PageSelectorItemDelegate final : public QStyledItemDelegate {
public:
    explicit PageSelectorItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        const QString text = itemOption.text;
        itemOption.text.clear();

        const QWidget* widget = itemOption.widget;
        QStyle* style = widget == nullptr ? QApplication::style() : widget->style();
        style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);

        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &itemOption, widget);
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        drawPageText(painter, textRect, text, itemOption.palette, (option.state & QStyle::State_Enabled) != 0,
                     selected);
    }
};
} // namespace

PageSelectorComboBox::PageSelectorComboBox(QWidget* parent) : QComboBox(parent)
{
    setItemDelegate(new PageSelectorItemDelegate(this));
    if (view() != nullptr) {
        view()->setTextElideMode(Qt::ElideMiddle);
    }
}

void PageSelectorComboBox::addPage(const QString& imageName, int pageIndex)
{
    const QString displayText = pageDisplayText(pageIndex, imageName);
    addItem(displayText, imageName);
    if (displayText.size() > m_longestDisplayText.size()) {
        m_longestDisplayText = displayText;
        invalidateSizeHint();
    }
}

void PageSelectorComboBox::clear()
{
    QComboBox::clear();
    m_longestDisplayText.clear();
    invalidateSizeHint();
}

QSize PageSelectorComboBox::minimumSizeHint() const
{
    return calculatedSizeHint();
}

QSize PageSelectorComboBox::sizeHint() const
{
    return calculatedSizeHint();
}

void PageSelectorComboBox::changeEvent(QEvent* event)
{
    QComboBox::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange ||
        event->type() == QEvent::PaletteChange) {
        invalidateSizeHint();
    }
}

void PageSelectorComboBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QStylePainter painter(this);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.currentText.clear();
    painter.drawComplexControl(QStyle::CC_ComboBox, option);

    const QRect textRect =
        style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this).adjusted(4, 0, -4, 0);
    drawPageText(&painter, textRect, currentText(), palette(), isEnabled());
}

QSize PageSelectorComboBox::calculatedSizeHint() const
{
    if (m_cachedSizeHint.isValid()) {
        return m_cachedSizeHint;
    }

    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QFontMetrics metrics(font());
    const QString displayText = m_longestDisplayText.isEmpty() ? QStringLiteral("000|000.png") : m_longestDisplayText;
    const int textWidth = std::clamp(metrics.horizontalAdvance(displayText) + pageBadgeHorizontalPadding * 2 + 16,
                                     pageSelectorMinimumTextWidth, pageSelectorMaximumTextWidth);
    const QSize contentSize(textWidth, std::max(metrics.height() + 8, 24));
    m_cachedSizeHint = style()->sizeFromContents(QStyle::CT_ComboBox, &option, contentSize, this);
    return m_cachedSizeHint;
}

void PageSelectorComboBox::invalidateSizeHint()
{
    m_cachedSizeHint = {};
    updateGeometry();
    if (view() != nullptr) {
        view()->updateGeometry();
    }
}
