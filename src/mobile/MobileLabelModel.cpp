#include "MobileLabelModel.h"
#include "services/label/LabelVisibility.h"
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
    static const core::LabelGroupStyle defaultStyle;
    const auto& style =
        row.groupIndex >= 0 && row.groupIndex < m_styles.size() ? m_styles.at(row.groupIndex) : defaultStyle;
    switch (role) {
    case Key:
        return row.label.id().toString(QUuid::WithoutBraces);
    case Text:
        return row.label.text();
    case Group:
        return row.label.group();
    case Number:
        return index.row() + 1;
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
    const auto indexes =
        services::LabelVisibility::sourceIndexes(labels, {visibleGroups.cbegin(), visibleGroups.cend()});
    rows.reserve(indexes.size());
    QHash<QString, int> groupIndexes;
    groupIndexes.reserve(allGroups.size());
    for (int i = 0; i < allGroups.size(); ++i)
        groupIndexes.insert(allGroups.at(i), i);
    for (int index : indexes) {
        const auto& label = labels.at(index);
        rows.append({label, groupIndexes.value(label.group(), -1)});
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
