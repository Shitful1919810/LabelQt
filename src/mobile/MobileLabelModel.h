#pragma once

#include "core/AppPreferences.h"
#include "core/Label.h"
#include <QAbstractListModel>
#include <QStringList>

namespace labelqt::mobile {

// Presentation snapshot only. All commands resolve stable identities in the document.
class MobileLabelModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Key = Qt::UserRole + 1, Text, Group, Number, PositionX, PositionY, MarkerColor, MarkerSquare };
    explicit MobileLabelModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setGroupStyles(QVector<core::LabelGroupStyle> styles);
    void refresh(const QVector<core::Label>& labels, const QStringList& visibleGroups, const QStringList& allGroups);

private:
    struct Row {
        core::Label label;
        int number;
        int groupIndex;
    };
    QVector<Row> m_rows;
    QVector<core::LabelGroupStyle> m_styles{core::defaultLabelGroupStyles()};
};
} // namespace labelqt::mobile
