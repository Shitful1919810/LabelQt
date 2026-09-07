#include "ui/project/ProjectRecoveryController.h"
#include "core/LabelPlusDocument.h"
#include "services/proofread/ProjectComparisonService.h"
#include "ui/project/ProjectRecoveryDialog.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include <QAction>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

using namespace labelqt::services;

ProjectRecoveryController::ProjectRecoveryController(QWidget* window, ProjectController& project, Callbacks callbacks,
                                                     QObject* parent)
    : QObject(parent), m_window(window), m_project(project), m_callbacks(std::move(callbacks)),
      m_action(new QAction(tr("Backup and Recovery..."), this)),
      m_notice(new QPushButton(tr("Recovery available..."), window))
{
    m_notice->hide();
    connect(m_action, &QAction::triggered, this, &ProjectRecoveryController::open);
    connect(m_notice, &QPushButton::clicked, this, &ProjectRecoveryController::open);
    connect(&project, &ProjectController::storageChanged, this, [this] {
        if (m_recoveryPath != m_project.project().filePath())
            check(m_project.project().filePath());
    });
}

QAction* ProjectRecoveryController::action() const
{
    return m_action;
}
QPushButton* ProjectRecoveryController::notice() const
{
    return m_notice;
}

void ProjectRecoveryController::check(const QString& projectPath)
{
    m_recoveryPath = projectPath;
    if (m_notice)
        m_notice->setVisible(ProjectRecoveryService::hasNewerRecovery(projectPath, m_callbacks.preferences()));
}

void ProjectRecoveryController::open()
{
    if (m_callbacks.blocked())
        return;
    ProjectRecoveryDialog dialog(
        m_recoveryPath.isEmpty() ? m_project.project().filePath() : m_recoveryPath, m_callbacks.preferences(),
        [this](const ProjectRecoveryEntry& entry) {
            if (!m_callbacks.prepareRecovery())
                return std::expected<void, QString>(std::unexpected(tr("Recovery canceled.")));
            m_callbacks.detachViews();
            const auto result = m_project.restoreBackup(entry, m_callbacks.preferences());
            m_callbacks.refreshViews(bool(result));
            return result;
        },
        m_callbacks.prepareRecovery, m_window);
    connect(&dialog, &ProjectRecoveryDialog::projectRecovered, this, [this](const QString& path) {
        if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_project.project().filePath()).absoluteFilePath())
            m_callbacks.reloadProject(path, {});
    });
    dialog.exec();
    check(m_recoveryPath);
}

bool ProjectRecoveryController::resolveSaveConflict(const ProjectFileConflict& conflict)
{
    return resolveDiskChange(conflict.revision);
}

bool ProjectRecoveryController::resolveDiskChange(std::optional<FileRevision> revision, const QString& error)
{
    if (m_callbacks.blocked())
        return false;
    m_callbacks.commitEditors();
    QMessageBox box(m_window);
    box.setWindowTitle(tr("Project changed on disk"));
    box.setText(tr("Loaded/saved: %1\nDisk version: %2")
                    .arg(m_project.savedRevision().modified.toLocalTime().toString(Qt::ISODate),
                         !error.isEmpty() ? error
                         : revision && revision->modified.isValid()
                             ? revision->modified.toLocalTime().toString(Qt::ISODate)
                             : tr("File missing")));
    box.setIcon(QMessageBox::Warning);
    auto* compare = box.addButton(tr("Compare"), QMessageBox::ActionRole);
    auto* copy = box.addButton(tr("Save current edits as..."), QMessageBox::ActionRole);
    auto* reload = box.addButton(tr("Reload disk version..."), QMessageBox::DestructiveRole);
    auto* overwrite = box.addButton(tr("Overwrite disk version..."), QMessageBox::DestructiveRole);
    compare->setEnabled(revision && revision->exists && error.isEmpty());
    reload->setEnabled(revision && revision->exists && error.isEmpty());
    overwrite->setEnabled(revision.has_value());
    box.setInformativeText(
        tr("The disk version differs from this session. Continuing to edit will not overwrite it automatically."));
    box.setStandardButtons(QMessageBox::Cancel);
    box.button(QMessageBox::Cancel)->setText(tr("Keep current version"));
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == copy)
        return m_callbacks.saveCopy();
    if (box.clickedButton() == reload) {
        QString message = tr("Reload the disk version? Undo history will be cleared.");
        if (m_project.isDirty()) {
            message += QLatin1Char('\n') + tr("Local edits will be backed up to %1. Backup failure cancels reloading.")
                                               .arg(ProjectRecoveryService::backupDirectory(
                                                   m_project.project().filePath(), m_callbacks.preferences()));
        }
        if (QMessageBox::warning(m_window, tr("Reload disk version"), message, QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel) != QMessageBox::Yes)
            return false;
        if (m_project.isDirty()) {
            const auto backup =
                ProjectRecoveryService::preserveLocalEdits(m_project.project(), m_callbacks.preferences());
            if (!backup) {
                QMessageBox::warning(m_window, tr("Reload canceled"), backup.error());
                return false;
            }
        }
        return m_callbacks.reloadProject(m_project.project().filePath(), revision);
    }
    try {
        if (box.clickedButton() == overwrite) {
            if (QMessageBox::warning(m_window, tr("Overwrite disk version"),
                                     tr("Replace the disk version with the current edits?"),
                                     QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
                return false;
            m_project.save(revision);
            return true;
        }
        if (box.clickedButton() == compare) {
            const auto disk = labelqt::core::LabelPlusDocument::loadFromFile(m_project.project().filePath());
            const auto current = m_project.project();
            const auto plan = ProjectComparisonService::planComparison(disk, current);
            ProofreadChangesDialog dialog(disk, current, m_callbacks.preferences(), plan,
                                          ProjectComparisonService::changesBetweenProjects(current, plan),
                                          {tr("Disk version"), tr("Current edits"), false}, m_window);
            dialog.exec();
        }
    }
    catch (const std::exception& error) {
        QMessageBox::warning(m_window, tr("Save failed"), QString::fromUtf8(error.what()));
    }
    return false;
}
