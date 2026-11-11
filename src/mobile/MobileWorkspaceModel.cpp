#include "MobileWorkspaceModel.h"

#include <QFileInfo>
#include <algorithm>

namespace labelqt::mobile {
int MobileWorkspaceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant MobileWorkspaceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
        index.row() >= rowCount())
        return {};
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case KeyRole:
        return entry.id;
    case TitleRole:
        return entry.title;
    case ModifiedRole:
        return entry.modifiedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> MobileWorkspaceModel::roleNames() const
{
    return {{KeyRole, "workspaceKey"}, {TitleRole, "workspaceTitle"}, {ModifiedRole, "modifiedAt"}};
}

void MobileWorkspaceModel::replace(QVector<services::WorkspaceInfo> entries)
{
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // Stable ID order lets updates preserve delegates and their open menus, without resets.
    int row = 0;
    for (auto& incoming : entries) {
        incoming.modifiedAt = QFileInfo(incoming.projectPath).lastModified();
        while (row < rowCount() && m_entries.at(row).id < incoming.id) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        }
        if (row == rowCount() || m_entries.at(row).id != incoming.id) {
            beginInsertRows({}, row, row);
            m_entries.insert(row, incoming);
            endInsertRows();
        }
        else {
            const auto previous = m_entries.at(row);
            m_entries[row] = incoming;
            if (previous.title != incoming.title || previous.modifiedAt != incoming.modifiedAt)
                emit dataChanged(index(row), index(row), {TitleRole, ModifiedRole});
        }
        ++row;
    }
    if (row < rowCount()) {
        beginRemoveRows({}, row, rowCount() - 1);
        m_entries.resize(row);
        endRemoveRows();
    }
}

void MobileWorkspaceModel::refreshSavedTime(const QString& projectPath)
{
    for (int row = 0; row < rowCount(); ++row) {
        auto& entry = m_entries[row];
        if (entry.projectPath != projectPath)
            continue;
        const auto time = QFileInfo(projectPath).lastModified();
        if (entry.modifiedAt != time) {
            entry.modifiedAt = time;
            emit dataChanged(index(row), index(row), {ModifiedRole});
        }
        break;
    }
}
} // namespace labelqt::mobile
