#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"

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

class ProjectController {
public:
    labelqt::core::Project& project() noexcept;
    const labelqt::core::Project& project() const noexcept;

    void setProject(labelqt::core::Project project);
    void loadFromFile(const QString& path);
    void save();
    void saveAs(const QString& path);
    NewProjectResult createProjectFromImageDirectory(const QString& directoryPath, const QString& projectBaseName,
                                                     const QStringList& defaultGroups, bool useNextAvailableName);

    bool isDirty() const noexcept;
    void setDirty(bool dirty) noexcept;
    void markDirty() noexcept;

    AutoBackupResult performAutoBackup(const labelqt::core::AppPreferences& preferences);

private:
    labelqt::core::Project m_project;
    bool m_isDirty{false};
    bool m_hasPendingBackup{false};
};

} // namespace labelqt::services
