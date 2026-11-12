#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <expected>
#include <functional>
#include <utility>

namespace labelqt::services {

struct WorkspaceInfo {
    QString id;
    QString title;
    QString projectPath;
    QString archiveName;
    QDateTime modifiedAt;
    bool reviewActive{false}; // Derived from TXT, never persisted in workspace.json.
    // Content inspection is advisory: damaged/oversized TXT files remain discoverable.
    QString diagnostic{};
};

// App-owned workspace metadata and disposable storage. Caller serializes mutations
// with transfers; a confirmed removal first detaches the directory atomically.
class WorkspaceStorageService final {
    Q_DECLARE_TR_FUNCTIONS(WorkspaceStorageService)
public:
    // Negative results mean unavailable storage information; production uses QStorageInfo.
    using AvailableBytes = std::function<qint64(const QString&)>;
    explicit WorkspaceStorageService(QString root, AvailableBytes availableBytes = {})
        : m_root(std::move(root)), m_availableBytes(std::move(availableBytes))
    {
    }
    std::expected<WorkspaceInfo, QString> rename(const QString& id, const QString& title) const;
    std::expected<void, QString> remove(const QString& id) const;
    std::expected<void, QString> cleanDisposableFiles(QDateTime now = QDateTime::currentDateTimeUtc()) const;
    static std::expected<QString, QString> archiveFileName(QString name);
    static std::expected<QString, QString> pdfFileName(QString name);
    // Revalidate a published ZIP/PDF before handoff. Caller serializes this with maintenance.
    std::expected<QString, QString> validateSharedFile(const QString& path) const;
    // Serialized with import/deletion/maintenance by the caller. Only complete files are published.
    // Estimate the entire simultaneous staging footprint, not just the final compressed output.
    // This is a preflight, not a filesystem reservation or a streaming write quota.
    std::expected<QString, QString>
    createSharedFile(const QString& fileName, const std::function<std::expected<void, QString>(const QString&)>& write,
                     const std::function<bool()>& canceled = {},
                     const std::function<qint64()>& estimatePeakBytes = {}) const;
    static QString suggestedArchiveName(QString name);
    static constexpr qint64 shareCacheLimit = 2LL * 1024 * 1024 * 1024;
    static constexpr qint64 saveSpaceReserve = 256LL * 1024 * 1024;

private:
    QString m_root;
    AvailableBytes m_availableBytes;
};

} // namespace labelqt::services
