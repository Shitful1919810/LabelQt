#include "services/project/ProjectController.h"

#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "core/ProjectIdentityService.h"
#include "services/io/FileRenameTransaction.h"
#include "services/project/ProjectLocationService.h"
#include "services/project/ProjectRecoveryService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QUuid>

#include <exception>
#include <expected>
#include <optional>
#include <stdexcept>
#include <utility>

namespace labelqt::services {

namespace {
FileRevision writeProject(const labelqt::core::Project& project, ProjectFileTransaction& transaction,
                          const FileRevision& expected)
{
    const QByteArray bytes = labelqt::core::LabelPlusDocument::serialize(project).toUtf8();
    return transaction.write(bytes, expected);
}

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
    if (!labelqt::core::ProjectIdentityService::valid(project))
        throw std::runtime_error(
            QCoreApplication::translate("LabelPlusDocument", "Invalid LabelPlus document structure.").toStdString());
    const auto revision = FileRevision::read(project.filePath());
    if (!revision)
        throw std::runtime_error(revision.error().toStdString());
    adoptProject(std::move(project), *revision);
}

void ProjectController::adoptProject(labelqt::core::Project project, FileRevision revision)
{
    m_project = std::move(project);
    m_documentSession = QUuid::createUuid();
    m_revisions.reset(m_project);
    m_savedRevision = std::move(revision);
    setDirty(false);
    emit storageChanged();
}

void ProjectController::loadFromFile(const QString& path, std::optional<FileRevision> expectedRevision)
{
    const auto recovered = FileRenameTransaction::checkRecoveryDirectory(QFileInfo(path).absolutePath());
    if (!recovered)
        throw std::runtime_error(recovered.error().toStdString());
    const auto before = FileRevision::read(path);
    if (!before)
        throw std::runtime_error(before.error().toStdString());
    if (expectedRevision && !before->sameContent(*expectedRevision))
        throw ProjectFileConflict(*before);
    auto loaded = labelqt::core::LabelPlusDocument::readFromFile(path);
    const auto after = FileRevision::read(path);
    if (!after)
        throw std::runtime_error(after.error().toStdString());
    if (!before->sameContent(*after) || loaded.sha256 != after->sha256)
        throw ProjectFileConflict(*after);
    adoptProject(std::move(loaded.project), *after);
}

void ProjectController::save(std::optional<FileRevision> confirmedRevision)
{
    ProjectFileTransaction transaction(m_project.filePath());
    save(transaction, confirmedRevision);
}

void ProjectController::save(ProjectFileTransaction& transaction, std::optional<FileRevision> confirmedRevision)
{
    const auto recovered =
        FileRenameTransaction::checkRecoveryDirectory(QFileInfo(m_project.filePath()).absolutePath());
    if (!recovered)
        throw std::runtime_error(recovered.error().toStdString());
    const auto revision = FileRevision::read(m_project.filePath());
    if (!revision)
        throw std::runtime_error(revision.error().toStdString());
    if (!revision->sameContent(confirmedRevision.value_or(m_savedRevision)))
        throw ProjectFileConflict(*revision);
    m_savedRevision = writeProject(m_project, transaction, confirmedRevision.value_or(m_savedRevision));
    setDirty(false);
    emit storageChanged();
}

void ProjectController::saveAs(const QString& path)
{
    if (ProjectFileTransaction::sameTarget(path, m_project.filePath())) {
        save();
        return;
    }
    ProjectFileTransaction transaction(path);
    const auto recovered = FileRenameTransaction::checkRecoveryDirectory(QFileInfo(path).absolutePath());
    if (!recovered)
        throw std::runtime_error(recovered.error().toStdString());
    const auto output = ProjectLocationService::forOutput(m_project, path);
    const auto expected = FileRevision::read(path);
    if (!expected)
        throw std::runtime_error(expected.error().toStdString());
    const auto revision = writeProject(output, transaction, *expected);
    m_project.setFilePath(path);
    m_project.setCommentLines(output.commentLines());
    m_revisions.reset(m_project);
    m_savedRevision = revision;
    setDirty(false);
    emit storageChanged();
}

const FileRevision& ProjectController::savedRevision() const
{
    return m_savedRevision;
}

std::expected<void, QString> ProjectController::restoreBackup(const ProjectRecoveryEntry& entry,
                                                              const labelqt::core::AppPreferences& preferences)
{
    if (QFileInfo(entry.projectPath).absoluteFilePath() != QFileInfo(m_project.filePath()).absoluteFilePath())
        return std::unexpected(
            QCoreApplication::translate("ProjectController", "Open the original project before replacing it."));
    const auto snapshot = ProjectRecoveryService::preview(entry);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    try {
        ProjectFileTransaction transaction(m_project.filePath());
        auto output = ProjectLocationService::forOutput(*snapshot, m_project.filePath());
        save(transaction);
        const QDir directory(ProjectRecoveryService::backupDirectory(m_project.filePath(), preferences));
        if (!QDir().mkpath(directory.absolutePath()))
            return std::unexpected(directory.absolutePath());
        const auto backup = ProjectRecoveryService::writeBackup(
            m_project,
            directory.filePath(
                QStringLiteral("before-restore-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))));
        if (!backup)
            return backup;
        const auto disk = FileRevision::read(m_project.filePath());
        if (!disk || !disk->sameContent(m_savedRevision))
            return std::unexpected(
                QCoreApplication::translate("ProjectController", "The project was changed on disk."));
        const auto revision = writeProject(output, transaction, m_savedRevision);
        adoptProject(std::move(output), revision);
        return {};
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<void, QString> ProjectController::renameImages(const ProjectImageRenamePlan& plan)
{
    try {
        ProjectFileTransaction transaction(m_project.filePath());
        save(transaction);
        const auto result = ProjectImageRenameService::applyPlan(
            m_project, plan,
            [this, &transaction](const labelqt::core::Project& updated) -> std::expected<void, QString> {
                const auto disk = FileRevision::read(updated.filePath());
                if (!disk)
                    return std::unexpected(disk.error());
                if (!disk->sameContent(m_savedRevision))
                    throw ProjectFileConflict(*disk);
                m_savedRevision = writeProject(updated, transaction, m_savedRevision);
                return {};
            });
        if (result) {
            setDirty(false);
            emit storageChanged();
        }
        return result;
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<QString, NewProjectError>
ProjectController::tryCreateProjectFromImageDirectory(const QString& directoryPath, const QString& projectBaseName,
                                                      const QStringList& defaultGroups, bool useNextAvailableName)
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
            return std::unexpected(NewProjectError{
                NewProjectError::Code::ProjectFileExists, QFileInfo(defaultProjectPath).fileName(), {}});
        }
        projectPath = availableProjectPath(directory, projectBaseName);
    }
    newProject.setFilePath(projectPath);

    try {
        ProjectFileTransaction transaction(projectPath);
        (void)writeProject(newProject, transaction, {});
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

const ProjectRevisionTracker& ProjectController::revisions()
{
    m_revisions.checkpoint(m_project);
    return m_revisions;
}

void ProjectController::setDirty(bool dirty)
{
    m_revisions.checkpoint(m_project);
    m_isDirty = dirty;
    if (!m_isDirty) {
        m_hasPendingBackup = false;
    }
}

void ProjectController::markDirty()
{
    m_hasPendingBackup = true;
    setDirty(true);
}

std::expected<std::optional<QString>, QString>
ProjectController::tryPerformAutoBackup(const labelqt::core::AppPreferences& preferences)
{
    if (!m_hasPendingBackup || !m_isDirty || m_project.isEmpty() || m_project.filePath().isEmpty()) {
        return std::optional<QString>{};
    }

    const QFileInfo projectInfo(m_project.filePath());
    const QString backupDirectoryPath = ProjectRecoveryService::backupDirectory(m_project.filePath(), preferences);
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
        const auto backup = ProjectRecoveryService::writeBackup(m_project, backupPath, true);
        if (!backup)
            return std::unexpected(backup.error());
        m_hasPendingBackup = false;
        ProjectRecoveryService::pruneAutomaticBackups(m_project.filePath(), preferences);
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
