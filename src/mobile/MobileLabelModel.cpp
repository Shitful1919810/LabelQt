#include "MobileLabelModel.h"
#include <algorithm>
#include <utility>

namespace labelqt::mobile {
int MobileLabelModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant MobileLabelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
        index.row() >= m_rows.size())
        return {};
    const auto& row = m_rows.at(index.row());
    const auto style =
        row.groupIndex >= 0 && row.groupIndex < m_styles.size() ? m_styles.at(row.groupIndex) : core::LabelGroupStyle{};
    switch (role) {
    case Key:
        return row.label.id().toString(QUuid::WithoutBraces);
    case Text:
        return row.label.text();
    case Group:
        return row.label.group();
    case Number:
        return row.number;
    case PositionX:
        return row.label.position().x();
    case PositionY:
        return row.label.position().y();
    case MarkerColor:
        return style.groupColor.isValid() ? style.groupColor : QColor(Qt::black);
    case MarkerSquare:
        return style.markerShape == core::MarkerShape::Square;
    default:
        return {};
    }
}

QHash<int, QByteArray> MobileLabelModel::roleNames() const
{
    return {{Key, "labelKey"},
            {Text, "labelText"},
            {Group, "groupName"},
            {Number, "markerNumber"},
            {PositionX, "positionX"},
            {PositionY, "positionY"},
            {MarkerColor, "markerColor"},
            {MarkerSquare, "markerSquare"}};
}

void MobileLabelModel::setGroupStyles(QVector<core::LabelGroupStyle> styles)
{
    m_styles = std::move(styles);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {MarkerColor, MarkerSquare});
}

void MobileLabelModel::refresh(const QVector<core::Label>& labels, const QStringList& visibleGroups,
                               const QStringList& allGroups)
{
    QVector<Row> rows;
    for (int i = 0; i < labels.size(); ++i) {
        if (!labels.at(i).isDeleted() && visibleGroups.contains(labels.at(i).group()))
            rows.append({labels.at(i), i + 1, static_cast<int>(allGroups.indexOf(labels.at(i).group()))});
    }
    const bool sameIdentities = rows.size() == m_rows.size() &&
                                std::equal(rows.begin(), rows.end(), m_rows.begin(),
                                           [](const Row& a, const Row& b) { return a.label.id() == b.label.id(); });
    if (!sameIdentities) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        const auto& before = m_rows.at(i);
        const auto& after = rows.at(i);
        QList<int> changed;
        if (before.label.text() != after.label.text())
            changed.append(Text);
        if (before.label.group() != after.label.group())
            changed.append(Group);
        if (before.number != after.number)
            changed.append(Number);
        if (before.label.position().x() != after.label.position().x())
            changed.append(PositionX);
        if (before.label.position().y() != after.label.position().y())
            changed.append(PositionY);
        if (before.groupIndex != after.groupIndex)
            changed.append({MarkerColor, MarkerSquare});
        m_rows[i] = after;
        if (!changed.isEmpty())
            emit dataChanged(index(i), index(i), changed);
    }
}
} // namespace labelqt::mobile
