#include "services/ProjectController.h"

#include "core/LabelPlusDocument.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <expected>
#include <exception>
#include <optional>
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

std::expected<QString, NewProjectError> ProjectController::tryCreateProjectFromImageDirectory(
    const QString& directoryPath, const QString& projectBaseName, const QStringList& defaultGroups,
    bool useNextAvailableName)
{
    QDir directory(directoryPath);
    const QFileInfoList imageFiles =
        directory.entryInfoList(supportedImageNameFilters(), QDir::Files | QDir::Readable, QDir::Name);
    if (imageFiles.isEmpty()) {
        return std::unexpected(NewProjectError{NewProjectError::Code::NoImages, {}, {}});
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
            return std::unexpected(
                NewProjectError{NewProjectError::Code::ProjectFileExists, QFileInfo(defaultProjectPath).fileName(), {}});
        }
        projectPath = availableProjectPath(directory, projectBaseName);
    }
    newProject.setFilePath(projectPath);

    try {
        labelqt::core::LabelPlusDocument::saveToFile(newProject, projectPath);
        return projectPath;
    }
    catch (const std::exception& error) {
        return std::unexpected(NewProjectError{NewProjectError::Code::Failed, {}, QString::fromUtf8(error.what())});
    }
}

NewProjectResult ProjectController::createProjectFromImageDirectory(const QString& directoryPath,
                                                                    const QString& projectBaseName,
                                                                    const QStringList& defaultGroups,
                                                                    bool useNextAvailableName)
{
    const std::expected<QString, NewProjectError> result =
        tryCreateProjectFromImageDirectory(directoryPath, projectBaseName, defaultGroups, useNextAvailableName);
    if (result.has_value()) {
        return {NewProjectResult::Status::Created, *result, {}, {}};
    }

    switch (result.error().code) {
    case NewProjectError::Code::NoImages:
        return {NewProjectResult::Status::NoImages, {}, {}, {}};
    case NewProjectError::Code::ProjectFileExists:
        return {NewProjectResult::Status::ProjectFileExists, {}, result.error().existingFileName, {}};
    case NewProjectError::Code::Failed:
        return {NewProjectResult::Status::Failed, {}, {}, result.error().message};
    }
    return {NewProjectResult::Status::Failed, {}, {}, {}};
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

std::expected<std::optional<QString>, QString> ProjectController::tryPerformAutoBackup(
    const labelqt::core::AppPreferences& preferences)
{
    if (!m_hasPendingBackup || !m_isDirty || m_project.isEmpty() || m_project.filePath().isEmpty()) {
        return std::optional<QString>{};
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
        return std::unexpected(backupDirectory.absolutePath());
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
        return backupPath;
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

AutoBackupResult ProjectController::performAutoBackup(const labelqt::core::AppPreferences& preferences)
{
    const std::expected<std::optional<QString>, QString> result = tryPerformAutoBackup(preferences);
    if (!result.has_value()) {
        return {AutoBackupResult::Status::Failed, {}, result.error()};
    }
    if (!result->has_value()) {
        return {};
    }
    return {AutoBackupResult::Status::Saved, **result, {}};
}

} // namespace labelqt::services
