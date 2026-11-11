#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <expected>
#include <utility>

namespace labelqt::services {

struct WorkspaceInfo {
    QString id;
    QString title;
    QString projectPath;
    QString archiveName;
    QDateTime modifiedAt;
};

// App-owned workspace metadata and disposable storage. Caller serializes mutations
// with transfers; a confirmed removal first detaches the directory atomically.
class WorkspaceStorageService final {
    Q_DECLARE_TR_FUNCTIONS(WorkspaceStorageService)
public:
    explicit WorkspaceStorageService(QString root) : m_root(std::move(root)) {}
    std::expected<WorkspaceInfo, QString> rename(const QString& id, const QString& title) const;
    std::expected<void, QString> remove(const QString& id) const;
    std::expected<void, QString> cleanDisposableFiles(QDateTime now = QDateTime::currentDateTimeUtc()) const;
    static std::expected<QString, QString> archiveFileName(QString name);
    static QString suggestedArchiveName(QString name);
    static constexpr qint64 shareCacheLimit = 2LL * 1024 * 1024 * 1024;

private:
    QString m_root;
};

} // namespace labelqt::services
