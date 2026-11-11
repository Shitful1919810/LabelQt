#pragma once

#include "services/transfer/WorkspaceStorageService.h"
#include <QAbstractListModel>

namespace labelqt::mobile {

class MobileWorkspaceModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, ModifiedRole };
    explicit MobileWorkspaceModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const QVector<services::WorkspaceInfo>& entries() const
    {
        return m_entries;
    }
    void replace(QVector<services::WorkspaceInfo> entries);
    void refreshSavedTime(const QString& projectPath);

private:
    QVector<services::WorkspaceInfo> m_entries;
};
} // namespace labelqt::mobile
