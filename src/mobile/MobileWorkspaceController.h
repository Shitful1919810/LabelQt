#pragma once

#include "MobileWorkspaceModel.h"
#include "mobile/MobileDocument.h"
#include "services/transfer/ProjectTransferService.h"
#include <QFutureWatcher>
#include <QTimer>

namespace labelqt::mobile {

class MobileWorkspaceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* workspaces READ workspaces CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY changed)
    Q_PROPERTY(QString suggestedShareName READ suggestedShareName NOTIFY changed)
public:
    MobileWorkspaceController(MobileDocument& document, QString root, QObject* parent = nullptr);
    ~MobileWorkspaceController() override;
    MobileWorkspaceModel* workspaces()
    {
        return &m_workspaces;
    }
    bool busy() const
    {
        return m_busy;
    }
    QString message() const
    {
        return m_message;
    }
    QString lastExport() const
    {
        return m_lastExport;
    }
    Q_INVOKABLE void importLocal(const QUrl& url);
    void importReceived(const QString& path, const QString& displayName = {});
    Q_INVOKABLE bool openWorkspace(const QString& id);
    Q_INVOKABLE bool exportProject(const QString& fileName);
    Q_INVOKABLE bool renameWorkspace(const QString& id, const QString& title);
    Q_INVOKABLE bool deleteWorkspace(const QString& id);
    QString suggestedShareName() const;
    Q_INVOKABLE void clearMessage();
    void showMessage(const QString& message);
signals:
    void changed();
    void exportReady(QString path);

private:
    struct Result {
        QString error;
        QString exportPath;
        QVector<services::WorkspaceInfo> workspaces;
    };
    struct Input {
        QString path;
        bool adjacentImages;
        bool inbox;
        QString displayName;
    };
    void import(Input input);
    void startNextImport();
    void startMaintenance();
    enum class Operation { Import, Export, Maintenance };
    Operation m_operation = Operation::Maintenance;
    QTimer m_cleanupTimer;
    MobileDocument& m_document;
    QString m_root;
    MobileWorkspaceModel m_workspaces;
    QFutureWatcher<Result> m_job;
    QList<Input> m_pending;
    QString m_message;
    QString m_lastExport;
    bool m_busy = false;
};
} // namespace labelqt::mobile
