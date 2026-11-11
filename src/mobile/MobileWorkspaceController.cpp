#include "MobileWorkspaceController.h"
#include <QFile>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>

namespace labelqt::mobile {
MobileWorkspaceController::MobileWorkspaceController(MobileDocument& document, QString root, QObject* parent)
    : QObject(parent), m_document(document), m_root(std::move(root))
{
    connect(&document.controller(), &services::ProjectController::storageChanged, this,
            [this] { m_workspaces.refreshSavedTime(m_document.controller().project().filePath()); });
    connect(&m_cleanupTimer, &QTimer::timeout, this, &MobileWorkspaceController::startMaintenance);
    m_cleanupTimer.start(60 * 60 * 1000);
    QTimer::singleShot(0, this, &MobileWorkspaceController::startMaintenance);
    connect(&m_job, &QFutureWatcher<Result>::finished, this, [this] {
        const auto completedOperation = m_operation;
        try {
            const auto result = m_job.result();
            m_workspaces.replace(result.workspaces);
            if (m_operation == Operation::Export)
                m_lastExport = result.exportPath;
            if (m_operation != Operation::Maintenance || !result.error.isEmpty())
                m_message = !result.error.isEmpty()            ? result.error
                            : m_operation == Operation::Import ? tr("Project imported. Select it to open.")
                                                               : QString{};
        }
        catch (const std::exception& e) {
            m_message = QString::fromUtf8(e.what());
            m_lastExport.clear();
        }
        catch (...) {
            m_message = tr("The file operation failed. Existing workspaces were kept.");
            m_lastExport.clear();
        }
        m_busy = false;
        const auto exportPath = completedOperation == Operation::Export ? m_lastExport : QString{};
        emit changed();
        if (!exportPath.isEmpty())
            emit exportReady(exportPath);
        QTimer::singleShot(0, this, &MobileWorkspaceController::startNextImport);
    });
}

MobileWorkspaceController::~MobileWorkspaceController()
{
    disconnect(&m_job, nullptr, this, nullptr);
    m_cleanupTimer.stop();
    disconnect(&m_cleanupTimer, nullptr, this, nullptr);
    disconnect(&m_document.controller(), nullptr, this, nullptr);
}

void MobileWorkspaceController::showMessage(const QString& message)
{
    m_message = message;
    emit changed();
}

void MobileWorkspaceController::clearMessage()
{
    showMessage({});
}

void MobileWorkspaceController::importLocal(const QUrl& url)
{
    if (!url.isLocalFile()) {
        showMessage(tr("Select a local ZIP or TXT file."));
        return;
    }
    import({url.toLocalFile(), true, false, {}});
}

void MobileWorkspaceController::importReceived(const QString& path, const QString& displayName)
{
    const auto inbox = QDir(m_root).filePath("inbox");
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.dir().canonicalPath() != QFileInfo(inbox).canonicalFilePath()) {
        showMessage(tr("The received file is outside the private inbox."));
        return;
    }
    import({path, false, true, displayName});
}

void MobileWorkspaceController::import(Input input)
{
    if (m_pending.size() >= 3) {
        showMessage(tr("Finish pending imports before adding more files."));
        return;
    }
    m_pending.append(std::move(input));
    startNextImport();
}

void MobileWorkspaceController::startNextImport()
{
    if (m_busy || m_pending.isEmpty())
        return;
    const Input input = m_pending.takeFirst();
    m_busy = true;
    m_operation = Operation::Import;
    m_lastExport.clear();
    m_message = tr("Importing a private copy...");
    emit changed();
    m_job.setFuture(QtConcurrent::run([root = m_root, input] {
        services::ProjectTransferService transfer(root);
        const auto result = transfer.importFile(input.path, input.adjacentImages, input.displayName);
        // Only transport-owned copies are released; picker/command-line source files are never removed.
        if (input.inbox)
            QFile::remove(input.path);
        return Result{result ? QString{} : result.error(), {}, transfer.listWorkspaces()};
    }));
}

bool MobileWorkspaceController::openWorkspace(const QString& id)
{
    if (m_busy)
        return false;
    for (const auto& workspace : m_workspaces.entries())
        if (workspace.id == id) {
            const bool opened = m_document.openProject(workspace.projectPath, workspace.title);
            if (opened)
                clearMessage();
            return opened;
        }
    showMessage(tr("The workspace is no longer available."));
    return false;
}

QString MobileWorkspaceController::suggestedShareName() const
{
    for (const auto& workspace : m_workspaces.entries())
        if (workspace.projectPath == m_document.controller().project().filePath())
            return workspace.archiveName;
    return services::WorkspaceStorageService::suggestedArchiveName(m_document.title());
}

bool MobileWorkspaceController::exportProject(const QString& fileName)
{
    if (m_busy || !m_document.opened() || !m_document.save())
        return false;
    const auto name = services::WorkspaceStorageService::archiveFileName(fileName);
    if (!name) {
        showMessage(name.error());
        return false;
    }
    const auto snapshot = m_document.controller().project();
    m_busy = true;
    m_operation = Operation::Export;
    m_lastExport.clear();
    m_message = tr("Preparing a verified ZIP...");
    emit changed();
    m_job.setFuture(QtConcurrent::run([root = m_root, snapshot, fileName = *name] {
        services::ProjectTransferService transfer(root);
        const auto cleaned = services::WorkspaceStorageService(root).cleanDisposableFiles();
        const auto result = transfer.exportZip(snapshot, fileName);
        if (!cleaned && result)
            return Result{cleaned.error(), *result, transfer.listWorkspaces()};
        return Result{result ? QString{} : result.error(), result ? *result : QString{}, transfer.listWorkspaces()};
    }));
    return true;
}

bool MobileWorkspaceController::renameWorkspace(const QString& id, const QString& title)
{
    if (m_busy)
        return false;
    const auto result = services::WorkspaceStorageService(m_root).rename(id, title);
    if (!result) {
        showMessage(result.error());
        return false;
    }
    auto entries = m_workspaces.entries();
    for (auto& workspace : entries)
        if (workspace.id == id)
            workspace = *result;
    m_workspaces.replace(std::move(entries));
    if (result->projectPath == m_document.controller().project().filePath())
        m_document.setTitle(result->title);
    clearMessage();
    return true;
}

bool MobileWorkspaceController::deleteWorkspace(const QString& id)
{
    if (m_busy)
        return false;
    auto entries = m_workspaces.entries();
    const auto it = std::find_if(entries.begin(), entries.end(), [&id](const auto& w) { return w.id == id; });
    if (it == entries.end())
        return false;
    const bool current = it->projectPath == m_document.controller().project().filePath();
    if (current && !m_document.save())
        return false;
    const auto result = services::WorkspaceStorageService(m_root).remove(id);
    if (!result) {
        showMessage(result.error());
        return false;
    }
    entries.erase(it);
    m_workspaces.replace(std::move(entries));
    if (current)
        m_document.closeRemovedProject();
    clearMessage();
    startMaintenance();
    return true;
}

void MobileWorkspaceController::startMaintenance()
{
    if (m_busy || !m_pending.isEmpty())
        return;
    m_operation = Operation::Maintenance;
    m_busy = true;
    emit changed();
    m_job.setFuture(QtConcurrent::run([root = m_root] {
        const auto result = services::WorkspaceStorageService(root).cleanDisposableFiles();
        return Result{result ? QString{} : result.error(), {}, services::ProjectTransferService(root).listWorkspaces()};
    }));
}
} // namespace labelqt::mobile
