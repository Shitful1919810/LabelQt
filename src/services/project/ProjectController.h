#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"

#include <expected>
#include <optional>

#include <QString>
#include <QStringList>

namespace labelqt::services {

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
    void save();
    void saveAs(const QString& path);
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
};

} // namespace labelqt::services
