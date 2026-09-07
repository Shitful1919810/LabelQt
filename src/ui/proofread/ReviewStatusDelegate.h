#pragma once

#include "services/proofread/ReviewSessionService.h"
#include <QIcon>
#include <QStyledItemDelegate>

class QStyle;

namespace labelqt::ui {
QIcon reviewStatusIcon(labelqt::services::ReviewStatus status, QStyle& style);
}

class ReviewStatusDelegate final : public QStyledItemDelegate {
public:
    explicit ReviewStatusDelegate(QWidget* parent);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    QVector<QIcon> m_icons;
};
