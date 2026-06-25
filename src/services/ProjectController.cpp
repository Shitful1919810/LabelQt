#include "services/ProjectController.h"

#include "core/LabelPlusDocument.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <exception>
#include <utility>

namespace labelqt::services {

namespace {
QStringList supportedImageNameFilters()
{
    QStringList filters;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    filters.reserve(formats.size());
    for (const QByteArray& format : formats) {
        const QString suffix = QString::fromLatin1(format);
        filters.append(QStringLiteral("*.%1").arg(suffix.toLower()));
        filters.append(QStringLiteral("*.%1").arg(suffix.toUpper()));
    }
    filters.removeDuplicates();
    filters.sort(Qt::CaseInsensitive);
    return filters;
}

QString availableProjectPath(const QDir& directory, const QString& baseName)
{
    const QString sanitizedBaseName = baseName.trimmed().isEmpty() ? QStringLiteral("project") : baseName.trimmed();
    QString candidate = directory.filePath(QStringLiteral("%1.txt").arg(sanitizedBaseName));
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }

    for (int i = 1;; ++i) {
        candidate = directory.filePath(QStringLiteral("%1_%2.txt").arg(sanitizedBaseName).arg(i));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}
} // namespace

labelqt::core::Project& ProjectController::project() noexcept
{
    return m_project;
}

const labelqt::core::Project& ProjectController::project() const noexcept
{
    return m_project;
}

void ProjectController::setProject(labelqt::core::Project project)
{
    m_project = std::move(project);
    setDirty(false);
}

void ProjectController::loadFromFile(const QString& path)
{
    setProject(labelqt::core::LabelPlusDocument::loadFromFile(path));
}

void ProjectController::save()
{
    labelqt::core::LabelPlusDocument::saveToFile(m_project, m_project.filePath());
    setDirty(false);
}

void ProjectController::saveAs(const QString& path)
{
    m_project.setFilePath(path);
    save();
}

NewProjectResult ProjectController::createProjectFromImageDirectory(const QString& directoryPath,
                                                                    const QString& projectBaseName,
                                                                    const QStringList& defaultGroups,
                                                                    bool useNextAvailableName)
{
    QDir directory(directoryPath);
    const QFileInfoList imageFiles =
        directory.entryInfoList(supportedImageNameFilters(), QDir::Files | QDir::Readable, QDir::Name);
    if (imageFiles.isEmpty()) {
        return {NewProjectResult::Status::NoImages, {}, {}, {}};
    }

    labelqt::core::Project newProject;
    newProject.setGroups(defaultGroups);
    newProject.setSourceName(directory.dirName());
    for (const QFileInfo& imageFile : imageFiles) {
        labelqt::core::ImageEntry image;
        image.name = imageFile.fileName();
        image.path = imageFile.absoluteFilePath();
        newProject.images().append(std::move(image));
    }

    const QString defaultProjectPath = directory.filePath(QStringLiteral("%1.txt").arg(projectBaseName));
    QString projectPath = defaultProjectPath;
    if (QFileInfo::exists(defaultProjectPath)) {
        if (!useNextAvailableName) {
            return {NewProjectResult::Status::ProjectFileExists, {}, QFileInfo(defaultProjectPath).fileName(), {}};
        }
        projectPath = availableProjectPath(directory, projectBaseName);
    }
    newProject.setFilePath(projectPath);

    try {
        labelqt::core::LabelPlusDocument::saveToFile(newProject, projectPath);
        return {NewProjectResult::Status::Created, projectPath, {}, {}};
    }
    catch (const std::exception& error) {
        return {NewProjectResult::Status::Failed, {}, {}, QString::fromUtf8(error.what())};
    }
}

bool ProjectController::isDirty() const noexcept
{
    return m_isDirty;
}

void ProjectController::setDirty(bool dirty) noexcept
{
    m_isDirty = dirty;
    if (!m_isDirty) {
        m_hasPendingBackup = false;
    }
}

void ProjectController::markDirty() noexcept
{
    m_hasPendingBackup = true;
    setDirty(true);
}

AutoBackupResult ProjectController::performAutoBackup(const labelqt::core::AppPreferences& preferences)
{
    if (!m_hasPendingBackup || !m_isDirty || m_project.isEmpty() || m_project.filePath().isEmpty()) {
        return {};
    }

    const QFileInfo projectInfo(m_project.filePath());
    const QString configuredBackupPath =
        preferences.backupPath().trimmed().isEmpty() ? QStringLiteral("bak") : preferences.backupPath().trimmed();
    const QFileInfo configuredBackupInfo(configuredBackupPath);
    const QString backupDirectoryPath = configuredBackupInfo.isAbsolute()
                                            ? configuredBackupInfo.absoluteFilePath()
                                            : projectInfo.absoluteDir().filePath(configuredBackupPath);
    QDir backupDirectory(backupDirectoryPath);
    if (!backupDirectory.exists() && !QDir().mkpath(backupDirectory.absolutePath())) {
        return {AutoBackupResult::Status::Failed, {}, backupDirectory.absolutePath()};
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString baseName =
        projectInfo.completeBaseName().isEmpty() ? QStringLiteral("project") : projectInfo.completeBaseName();
    const QString suffix = projectInfo.suffix().isEmpty() ? QStringLiteral("txt") : projectInfo.suffix();
    QString backupPath = backupDirectory.filePath(QStringLiteral("%1_%2.%3").arg(baseName, timestamp, suffix));
    for (int i = 1; QFileInfo::exists(backupPath); ++i) {
        backupPath =
            backupDirectory.filePath(QStringLiteral("%1_%2_%3.%4").arg(baseName, timestamp).arg(i).arg(suffix));
    }

    try {
        labelqt::core::LabelPlusDocument::saveToFile(m_project, backupPath);
        m_hasPendingBackup = false;
        return {AutoBackupResult::Status::Saved, backupPath, {}};
    }
    catch (const std::exception& error) {
        return {AutoBackupResult::Status::Failed, {}, QString::fromUtf8(error.what())};
    }
}

} // namespace labelqt::services
