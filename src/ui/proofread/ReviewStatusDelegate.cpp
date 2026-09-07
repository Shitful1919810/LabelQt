#include "ui/proofread/ReviewStatusDelegate.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include <QApplication>
#include <QPainter>
#include <QStyle>

QIcon labelqt::ui::reviewStatusIcon(labelqt::services::ReviewStatus status, QStyle& style)
{
    using labelqt::services::ReviewStatus;
    const auto symbol = status == ReviewStatus::Reviewed            ? QStyle::SP_DialogApplyButton
                        : status == ReviewStatus::NeedsConfirmation ? QStyle::SP_MessageBoxWarning
                                                                    : QStyle::SP_MessageBoxQuestion;
    const QColor color = status == ReviewStatus::Reviewed            ? QColor("#299c68")
                         : status == ReviewStatus::NeedsConfirmation ? QColor("#d69a19")
                                                                     : QColor("#db505c");
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48}) {
        auto pixmap = style.standardIcon(symbol).pixmap(size, size);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

ReviewStatusDelegate::ReviewStatusDelegate(QWidget* parent) : QStyledItemDelegate(parent)
{
    for (auto status : {labelqt::services::ReviewStatus::Unseen, labelqt::services::ReviewStatus::Reviewed,
                        labelqt::services::ReviewStatus::NeedsConfirmation})
        m_icons.append(labelqt::ui::reviewStatusIcon(status, *parent->style()));
}

void ReviewStatusDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem background(option);
    initStyleOption(&background, index);
    background.text.clear();
    const auto* widget = option.widget;
    auto* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

    const int status = index.data(ProofreadChangeTableModel::ReviewStatusRole).toInt();
    const int size = style->pixelMetric(QStyle::PM_SmallIconSize, nullptr, widget);
    const QRect logicalIcon(option.rect.left() + 6, option.rect.center().y() - size / 2, size, size);
    const QRect iconRect = QStyle::visualRect(option.direction, option.rect, logicalIcon);
    const QRect textRect = QStyle::visualRect(option.direction, option.rect, option.rect.adjusted(size + 12, 0, -6, 0));
    painter->save();
    painter->setClipRect(option.rect);
    m_icons.value(status).paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);
    painter->setFont(option.font);
    painter->setPen(
        option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeading,
                      option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));
    painter->restore();
}
