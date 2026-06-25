#include "ui/LabelTableModel.h"

#include <QBrush>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

#include <algorithm>
#include <utility>

namespace {
constexpr auto labelRowsMimeType = "application/x-labelqt-label-rows";
}

LabelTableModel::LabelTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void LabelTableModel::setLabels(QVector<labelqt::core::Label>* labels)
{
    beginResetModel();
    m_labels = labels;
    rebuildVisibleRows();
    endResetModel();
}

void LabelTableModel::setGroupFilter(QStringList groupFilter)
{
    beginResetModel();
    m_groupFilter = QSet<QString>(groupFilter.cbegin(), groupFilter.cend());
    rebuildVisibleRows();
    endResetModel();
}

void LabelTableModel::setGroups(QStringList groups, QVector<labelqt::core::LabelGroupStyle> groupStyles)
{
    m_groups = std::move(groups);
    m_groupStyles = std::move(groupStyles);
    if (rowCount() > 0) {
        emit dataChanged(index(0, NumberColumn), index(rowCount() - 1, columnCount() - 1));
    }
}

void LabelTableModel::refresh()
{
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

void LabelTableModel::labelChanged(int row)
{
    const int visibleRow = rowForSourceIndex(row);
    if (visibleRow < 0) {
        return;
    }

    emit dataChanged(index(visibleRow, NumberColumn), index(visibleRow, columnCount() - 1));
}

int LabelTableModel::sourceIndexForRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_visibleRows.size())) {
        return -1;
    }
    return m_visibleRows.at(row);
}

int LabelTableModel::rowForSourceIndex(int sourceIndex) const
{
    for (int row = 0; row < static_cast<int>(m_visibleRows.size()); ++row) {
        if (m_visibleRows.at(row) == sourceIndex) {
            return row;
        }
    }
    return -1;
}

int LabelTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_labels == nullptr) {
        return 0;
    }
    return static_cast<int>(m_visibleRows.size());
}

int LabelTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LabelTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || m_labels == nullptr || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return {};
    }

    const int sourceIndex = m_visibleRows.at(index.row());
    const labelqt::core::Label& label = m_labels->at(sourceIndex);
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case NumberColumn:
            return index.row() + 1;
        case TextColumn:
            return label.text();
        case GroupColumn:
            return label.group();
        default:
            return {};
        }
    }

    if (role == Qt::TextAlignmentRole && index.column() != TextColumn) {
        return Qt::AlignCenter;
    }

    if (role == Qt::ForegroundRole && index.column() == GroupColumn) {
        const QColor color = colorForGroup(label.group());
        if (color.isValid()) {
            return QBrush(color);
        }
    }

    return {};
}

bool LabelTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || m_labels == nullptr || index.row() < 0 ||
        index.row() >= m_visibleRows.size()) {
        return false;
    }

    const int sourceIndex = m_visibleRows.at(index.row());
    const labelqt::core::Label& label = m_labels->at(sourceIndex);
    QVariant newValue;

    switch (index.column()) {
    case TextColumn: {
        const QString text = value.toString();
        if (label.text() == text) {
            return false;
        }
        newValue = text;
        break;
    }
    case GroupColumn: {
        const QString group = value.toString();
        if (!m_groups.contains(group) || label.group() == group) {
            return false;
        }
        newValue = group;
        break;
    }
    default:
        return false;
    }

    emit labelEditRequested(sourceIndex, index.column(), newValue);
    return true;
}

Qt::ItemFlags LabelTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (!index.isValid()) {
        return itemFlags | Qt::ItemIsDropEnabled;
    }

    itemFlags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    if (index.isValid() && (index.column() == TextColumn || index.column() == GroupColumn)) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

Qt::DropActions LabelTableModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions LabelTableModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList LabelTableModel::mimeTypes() const
{
    return {QString::fromLatin1(labelRowsMimeType)};
}

QMimeData* LabelTableModel::mimeData(const QModelIndexList& indexes) const
{
    auto* mimeData = new QMimeData;
    QVector<int> sourceIndexes;
    QVector<int> rows;
    rows.reserve(indexes.size());

    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            rows.append(index.row());
        }
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    sourceIndexes.reserve(rows.size());
    for (int row : rows) {
        const int sourceIndex = sourceIndexForRow(row);
        if (sourceIndex >= 0) {
            sourceIndexes.append(sourceIndex);
        }
    }

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << sourceIndexes;
    mimeData->setData(QString::fromLatin1(labelRowsMimeType), data);
    return mimeData;
}

bool LabelTableModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                      const QModelIndex& parent) const
{
    Q_UNUSED(row)
    Q_UNUSED(parent)

    if (action == Qt::IgnoreAction) {
        return true;
    }
    return action == Qt::MoveAction && column <= 0 && data != nullptr &&
           data->hasFormat(QString::fromLatin1(labelRowsMimeType));
}

bool LabelTableModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                   const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent)) {
        return false;
    }
    if (action == Qt::IgnoreAction) {
        return true;
    }

    QByteArray encodedRows = data->data(QString::fromLatin1(labelRowsMimeType));
    QDataStream stream(&encodedRows, QIODevice::ReadOnly);
    QVector<int> sourceIndexes;
    stream >> sourceIndexes;
    if (sourceIndexes.isEmpty()) {
        return false;
    }

    int visibleDropRow = row;
    if (visibleDropRow < 0 && parent.isValid()) {
        visibleDropRow = parent.row();
    }
    if (visibleDropRow < 0) {
        visibleDropRow = rowCount();
    }
    visibleDropRow = std::clamp(visibleDropRow, 0, rowCount());

    emit labelsReorderRequested(sourceIndexes, visibleDropRow);
    return true;
}

void LabelTableModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    if (m_labels == nullptr) {
        return;
    }

    for (int i = 0; i < static_cast<int>(m_labels->size()); ++i) {
        const labelqt::core::Label& label = m_labels->at(i);
        if (!label.isDeleted() && m_groupFilter.contains(label.group())) {
            m_visibleRows.append(i);
        }
    }
}

QColor LabelTableModel::colorForGroup(const QString& group) const
{
    const int index = static_cast<int>(m_groups.indexOf(group));
    if (index < 0 || index >= static_cast<int>(m_groupStyles.size())) {
        return {};
    }
    return m_groupStyles.at(index).groupColor;
}

QVariant LabelTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case NumberColumn:
        return QStringLiteral("#");
    case TextColumn:
        return tr("Text");
    case GroupColumn:
        return tr("Group");
    default:
        return {};
    }
}
