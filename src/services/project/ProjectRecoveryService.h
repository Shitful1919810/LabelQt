#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include <QDateTime>
#include <expected>

namespace labelqt::services {

struct ProjectRecoveryEntry {
    enum class Kind { Backup, Rename };
    Kind kind{Kind::Backup};
    QString recordPath;
    QString projectPath;
    QString snapshotPath;
    QDateTime created;
    QByteArray digest;
    bool automatic{false};
};

class ProjectRecoveryService final {
public:
    static QString backupDirectory(const QString& projectPath, const labelqt::core::AppPreferences& preferences);
    static std::expected<void, QString> writeBackup(const labelqt::core::Project& project, const QString& path,
                                                    bool automatic = false);
    static void pruneAutomaticBackups(const QString& projectPath, const labelqt::core::AppPreferences& preferences);
    static QVector<ProjectRecoveryEntry> entries(const QString& projectPath,
                                                 const labelqt::core::AppPreferences& preferences);
    static std::expected<labelqt::core::Project, QString> preview(const ProjectRecoveryEntry& entry);
    static std::expected<void, QString> restoreCopy(const ProjectRecoveryEntry& entry, const QString& target);
    static bool hasNewerRecovery(const QString& projectPath, const labelqt::core::AppPreferences& preferences);
    static std::expected<void, QString> recoverFiles(const QString& recordPath, bool complete);
};

} // namespace labelqt::services
