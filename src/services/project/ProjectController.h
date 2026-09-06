#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/io/FileRevision.h"
#include "services/project/ProjectImageRenameService.h"

#include <expected>
#include <optional>
#include <stdexcept>

#include <QString>
#include <QStringList>

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
    labelqt::core::Project& project() noexcept;
    const labelqt::core::Project& project() const noexcept;

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
    void setDirty(bool dirty) noexcept;
    void markDirty() noexcept;

    std::expected<std::optional<QString>, QString> tryPerformAutoBackup(
        const labelqt::core::AppPreferences& preferences);
    AutoBackupResult performAutoBackup(const labelqt::core::AppPreferences& preferences);

private:
    labelqt::core::Project m_project;
    bool m_isDirty{false};
    bool m_hasPendingBackup{false};
    FileRevision m_savedRevision;
};

} // namespace labelqt::services
