#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/io/FileRevision.h"
#include "services/project/ProjectImageRenameService.h"
#include "services/project/ProjectRevisionTracker.h"

#include <expected>
#include <optional>
#include <stdexcept>

#include <QString>
#include <QStringList>
#include <QThread>

namespace labelqt::services {
struct ProjectRecoveryEntry;

class ProjectFileConflict final : public std::runtime_error {
public:
    explicit ProjectFileConflict(FileRevision diskRevision);
    const FileRevision revision;
};

struct AutoBackupResult {
    enum class Status {
        Skipped,
        Saved,
        Failed,
    };

    Status status{Status::Skipped};
    QString path;
    QString error;
};

struct NewProjectResult {
    enum class Status {
        Created,
        NoImages,
        ProjectFileExists,
        Failed,
    };

    Status status{Status::Failed};
    QString projectPath;
    QString existingFileName;
    QString error;
};

struct NewProjectError {
    enum class Code {
        NoImages,
        ProjectFileExists,
        Failed,
    };

    Code code{Code::Failed};
    QString existingFileName;
    QString message;
};

class ProjectController {
public:
    ProjectController() = default;
    ProjectController(const ProjectController&) = delete;
    ProjectController& operator=(const ProjectController&) = delete;
    labelqt::core::Project& project() noexcept;
    const labelqt::core::Project& project() const noexcept;
    const ProjectRevisionTracker& revisions();
    bool isOwnerThread() const noexcept
    {
        return QThread::currentThread() == m_ownerThread;
    }

    void setProject(labelqt::core::Project project);
    void loadFromFile(const QString& path);
    void save(std::optional<FileRevision> confirmedRevision = {});
    void saveAs(const QString& path);
    const FileRevision& savedRevision() const;
    std::expected<void, QString> restoreBackup(const ProjectRecoveryEntry& entry,
                                               const labelqt::core::AppPreferences& preferences);
    std::expected<void, QString> renameImages(const ProjectImageRenamePlan& plan);
    std::expected<QString, NewProjectError> tryCreateProjectFromImageDirectory(const QString& directoryPath,
                                                                               const QString& projectBaseName,
                                                                               const QStringList& defaultGroups,
                                                                               bool useNextAvailableName);
    NewProjectResult createProjectFromImageDirectory(const QString& directoryPath, const QString& projectBaseName,
                                                     const QStringList& defaultGroups, bool useNextAvailableName);

    bool isDirty() const noexcept;
    void setDirty(bool dirty);
    void markDirty();

    std::expected<std::optional<QString>, QString> tryPerformAutoBackup(
        const labelqt::core::AppPreferences& preferences);
    AutoBackupResult performAutoBackup(const labelqt::core::AppPreferences& preferences);

private:
    labelqt::core::Project m_project;
    QThread* const m_ownerThread{QThread::currentThread()};
    ProjectRevisionTracker m_revisions;
    bool m_isDirty{false};
    bool m_hasPendingBackup{false};
    FileRevision m_savedRevision;
};

} // namespace labelqt::services
