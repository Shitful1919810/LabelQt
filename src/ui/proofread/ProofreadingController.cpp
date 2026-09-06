#include "ui/proofread/ProofreadingController.h"

#include "core/LabelPlusDocument.h"
#include "services/project/ProjectLocationService.h"
#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ReviewMetadataService.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include "ui/widgets/BackgroundTask.h"
#include "ui/widgets/BusyTaskDialog.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

#include <optional>
#include <stdexcept>
#include <utility>

namespace {
QString text(const char* source)
{
    return QCoreApplication::translate("MainWindow", source);
}

[[maybe_unused]] constexpr const char* mainWindowTranslationSources[] = {
    QT_TRANSLATE_NOOP("MainWindow", "Unknown error"),
    QT_TRANSLATE_NOOP("MainWindow", "Comparing projects..."),
    QT_TRANSLATE_NOOP("MainWindow", "Abort"),
    QT_TRANSLATE_NOOP("MainWindow", "Proofreading"),
    QT_TRANSLATE_NOOP("MainWindow", "Open a project before starting proofreading."),
    QT_TRANSLATE_NOOP("MainWindow", "Start proofreading baseline"),
    QT_TRANSLATE_NOOP("MainWindow", "Proofreading baseline updated."),
    QT_TRANSLATE_NOOP("MainWindow", "End proofreading"),
    QT_TRANSLATE_NOOP("MainWindow", "Proofreading ended."),
    QT_TRANSLATE_NOOP("MainWindow", "Open a project before viewing proofreading changes."),
    QT_TRANSLATE_NOOP("MainWindow", "Start a proofreading baseline before viewing changes."),
    QT_TRANSLATE_NOOP("MainWindow", "No proofreading changes since the baseline."),
    QT_TRANSLATE_NOOP("MainWindow", "Compare Projects"),
    QT_TRANSLATE_NOOP("MainWindow", "Open a project before comparing."),
    QT_TRANSLATE_NOOP("MainWindow", "Select project to compare"),
    QT_TRANSLATE_NOOP("MainWindow", "LabelPlus Text (*.txt);;All Files (*)"),
    QT_TRANSLATE_NOOP("MainWindow", "Failed to open comparison project: %1"),
    QT_TRANSLATE_NOOP("MainWindow", "Failed to compare projects: %1"),
    QT_TRANSLATE_NOOP("MainWindow",
                      "The selected project has very few page names in common with the current project, but both "
                      "projects have the same number of pages. Compare pages by their current order instead?"),
    QT_TRANSLATE_NOOP("MainWindow", "No differences from the selected project."),
    QT_TRANSLATE_NOOP("MainWindow", "Current Project"),
    QT_TRANSLATE_NOOP("MainWindow", "Target Project"),
    QT_TRANSLATE_NOOP("MainWindow", "End Proofreading"),
    QT_TRANSLATE_NOOP("MainWindow", "Start Proofreading Baseline"),
};

struct ComparisonTaskResult {
    labelqt::services::ProjectComparisonPlan plan;
    QVector<labelqt::services::ReviewChange> changes;
    QString error;
};

ComparisonTaskResult buildComparison(labelqt::core::Project baseline, labelqt::core::Project current,
                                     labelqt::services::ProjectPageMatchMode mode)
{
    ComparisonTaskResult result;
    try {
        result.plan = labelqt::services::ProjectComparisonService::planComparison(baseline, current, mode);
        result.changes = labelqt::services::ProjectComparisonService::changesBetweenProjects(current, result.plan);
    }
    catch (const std::exception& error) {
        result.error = QString::fromLocal8Bit(error.what());
    }
    catch (...) {
        result.error = text("Unknown error");
    }
    return result;
}

std::optional<ComparisonTaskResult> runComparison(QWidget* parent, const labelqt::core::Project& baseline,
                                                  const labelqt::core::Project& current,
                                                  labelqt::services::ProjectPageMatchMode mode)
{
    labelqt::ui::BusyTaskDialog dialog(text("Comparing projects..."), text("Abort"), parent);
    return labelqt::ui::waitForBackgroundTask(QtConcurrent::run(buildComparison, baseline, current, mode), dialog);
}

QMessageBox::StandardButton messageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title,
                                       const QString& message, QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                       QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    QMessageBox dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setText(message);
    dialog.setIcon(icon);
    dialog.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton)
        dialog.setDefaultButton(defaultButton);
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    return static_cast<QMessageBox::StandardButton>(dialog.exec());
}

void information(QWidget* parent, const QString& title, const QString& message)
{
    messageBox(parent, QMessageBox::Information, title, message);
}

void warning(QWidget* parent, const QString& title, const QString& message)
{
    messageBox(parent, QMessageBox::Warning, title, message);
}
} // namespace

ProofreadingController::ProofreadingController(QWidget* parentWidget, labelqt::core::UndoStack& undoStack,
                                               Callbacks callbacks, QObject* parent)
    : QObject(parent), m_parentWidget(parentWidget), m_undoStack(undoStack), m_callbacks(std::move(callbacks))
{
}

void ProofreadingController::setUi(QAction* toggleAction, QAction* showChangesAction, QAction* compareAction,
                                   QLabel* statusLabel)
{
    m_toggleAction = toggleAction;
    m_showChangesAction = showChangesAction;
    m_compareAction = compareAction;
    m_statusLabel = statusLabel;
}

bool ProofreadingController::isActive() const
{
    return m_callbacks.project && labelqt::services::ReviewMetadataService::projectHasBaseline(m_callbacks.project());
}

void ProofreadingController::toggleBaseline()
{
    if (!m_callbacks.project || (m_callbacks.isEditingBlocked && m_callbacks.isEditingBlocked()))
        return;
    if (m_callbacks.commitActiveTextInput)
        m_callbacks.commitActiveTextInput();
    auto& project = m_callbacks.project();
    if (project.images().isEmpty()) {
        information(m_parentWidget, text("Proofreading"), text("Open a project before starting proofreading."));
        return;
    }
    if (isActive()) {
        endBaseline();
        return;
    }

    const QStringList oldLines = project.commentLines();
    const QString metadataPath = project.filePath();
    const QStringList newLines = labelqt::services::ReviewSessionService::withProjectSession(
        labelqt::services::ReviewMetadataService::rewriteCommentLines(
            oldLines, labelqt::services::ReviewMetadataService::captureBaseline(project)),
        {});
    if (oldLines == newLines)
        return;
    project.setCommentLines(newLines);
    const QString message = text("Start proofreading baseline");
    m_undoStack.push(
        message, message, message,
        [this, oldLines, metadataPath]() {
            m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                oldLines, metadataPath, m_callbacks.project().filePath()));
            updateUi(m_automationRunning);
            m_callbacks.markDirty();
        },
        [this, newLines, metadataPath]() {
            m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                newLines, metadataPath, m_callbacks.project().filePath()));
            updateUi(m_automationRunning);
            m_callbacks.markDirty();
        });
    if (m_callbacks.markDirty)
        m_callbacks.markDirty();
    updateUi(m_automationRunning);
    if (m_callbacks.showStatus)
        m_callbacks.showStatus(text("Proofreading baseline updated."), 4000);
}

void ProofreadingController::endBaseline()
{
    auto& project = m_callbacks.project();
    const QStringList oldLines = project.commentLines();
    const QString metadataPath = project.filePath();
    const QStringList newLines = labelqt::services::ReviewSessionService::withProjectSession(
        labelqt::services::ReviewMetadataService::rewriteCommentLines(oldLines, {}), {});
    if (oldLines == newLines) {
        updateUi(m_automationRunning);
        return;
    }
    project.setCommentLines(newLines);
    const QString message = text("End proofreading");
    m_undoStack.push(
        message, message, message,
        [this, oldLines, metadataPath]() {
            m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                oldLines, metadataPath, m_callbacks.project().filePath()));
            updateUi(m_automationRunning);
            m_callbacks.markDirty();
        },
        [this, newLines, metadataPath]() {
            m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                newLines, metadataPath, m_callbacks.project().filePath()));
            updateUi(m_automationRunning);
            m_callbacks.markDirty();
        });
    if (m_callbacks.markDirty)
        m_callbacks.markDirty();
    updateUi(m_automationRunning);
    if (m_callbacks.showStatus)
        m_callbacks.showStatus(text("Proofreading ended."), 4000);
}

void ProofreadingController::showChanges()
{
    if (m_callbacks.commitActiveTextInput)
        m_callbacks.commitActiveTextInput();
    auto& project = m_callbacks.project();
    if (project.images().isEmpty()) {
        information(m_parentWidget, text("Proofreading"), text("Open a project before viewing proofreading changes."));
        return;
    }
    auto metadata = labelqt::services::ReviewMetadataService::metadataForProject(project);
    if (!metadata.hasBaseline()) {
        information(m_parentWidget, text("Proofreading"),
                    text("Start a proofreading baseline before viewing changes."));
        return;
    }
    auto changes = labelqt::services::ReviewMetadataService::changesForProject(project, metadata);
    if (changes.isEmpty()) {
        information(m_parentWidget, text("Proofreading"), text("No proofreading changes since the baseline."));
        return;
    }
    ProofreadChangesDialog dialog(
        project, project, m_callbacks.preferences(), std::move(metadata), std::move(changes),
        {.leftProjectTitle = {}, .rightProjectTitle = {}, .reviewEnabled = true, .projectReviewSession = true},
        m_parentWidget);
    const int result = dialog.exec();
    const auto originalSession = labelqt::services::ReviewSessionService::decode(
        labelqt::services::ReviewSessionService::projectSession(project));
    if (originalSession &&
        dialog.reviewSession() != labelqt::services::ReviewSessionService::encode(*originalSession)) {
        const auto before = project.commentLines();
        const QString metadataPath = project.filePath();
        const auto after = labelqt::services::ReviewSessionService::withProjectSession(before, dialog.reviewSession());
        project.setCommentLines(after);
        m_undoStack.push(
            tr("Edit review session"),
            [this, before, metadataPath] {
                m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                    before, metadataPath, m_callbacks.project().filePath()));
                m_callbacks.markDirty();
            },
            [this, after, metadataPath] {
                m_callbacks.project().setCommentLines(labelqt::services::ProjectLocationService::rebaseComments(
                    after, metadataPath, m_callbacks.project().filePath()));
                m_callbacks.markDirty();
            });
        m_callbacks.markDirty();
    }
    if (result == QDialog::Accepted)
        selectDialogResult(dialog.selectedImageIndex(), dialog.selectedLabelIndex());
}

void ProofreadingController::compareWithProject()
{
    if (m_callbacks.commitActiveTextInput)
        m_callbacks.commitActiveTextInput();
    auto& project = m_callbacks.project();
    if (project.images().isEmpty()) {
        information(m_parentWidget, text("Compare Projects"), text("Open a project before comparing."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        m_parentWidget, text("Select project to compare"),
        m_sessionStateStore.lastFileDialogDirectory(labelqt::services::FileDialogScope::CompareProject),
        text("LabelPlus Text (*.txt);;All Files (*)"));
    if (path.isEmpty())
        return;
    m_sessionStateStore.saveLastFileDialogPath(labelqt::services::FileDialogScope::CompareProject, path);

    labelqt::core::Project target;
    try {
        target = labelqt::core::LabelPlusDocument::loadFromFile(path);
    }
    catch (const std::exception& error) {
        warning(m_parentWidget, text("Compare Projects"),
                text("Failed to open comparison project: %1").arg(QString::fromLocal8Bit(error.what())));
        return;
    }
    const labelqt::core::Project current = project;
    auto result = runComparison(m_parentWidget, current, target, labelqt::services::ProjectPageMatchMode::ByName);
    if (!result)
        return;
    if (!result->error.isEmpty()) {
        warning(m_parentWidget, text("Compare Projects"), text("Failed to compare projects: %1").arg(result->error));
        return;
    }
    if (labelqt::services::ProjectComparisonService::shouldOfferOrderPageMatching(result->plan) &&
        messageBox(m_parentWidget, QMessageBox::Question, text("Compare Projects"),
                   text("The selected project has very few page names in common with the current project, but both "
                        "projects have the same number of pages. Compare pages by their current order instead?"),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        result = runComparison(m_parentWidget, current, target, labelqt::services::ProjectPageMatchMode::ByOrder);
        if (!result)
            return;
        if (!result->error.isEmpty()) {
            warning(m_parentWidget, text("Compare Projects"),
                    text("Failed to compare projects: %1").arg(result->error));
            return;
        }
    }
    if (result->changes.isEmpty()) {
        information(m_parentWidget, text("Compare Projects"), text("No differences from the selected project."));
        return;
    }
    ProofreadChangesDialog dialog(current, target, m_callbacks.preferences(), std::move(result->plan.baselineMetadata),
                                  std::move(result->changes),
                                  {.leftProjectTitle = text("Current Project"),
                                   .rightProjectTitle = text("Target Project"),
                                   .jumpToLeftProject = true,
                                   .reviewEnabled = true},
                                  m_parentWidget);
    if (dialog.exec() == QDialog::Accepted)
        selectDialogResult(dialog.selectedImageIndex(), dialog.selectedLabelIndex());
}

void ProofreadingController::selectDialogResult(int imageIndex, int labelIndex)
{
    if (!m_callbacks.project || !m_callbacks.selectResult)
        return;
    const auto& images = m_callbacks.project().images();
    if (imageIndex < 0 || imageIndex >= images.size())
        return;
    const auto& labels = images.at(imageIndex).labels;
    const int validLabel =
        labelIndex >= 0 && labelIndex < labels.size() && !labels.at(labelIndex).isDeleted() ? labelIndex : -1;
    m_callbacks.selectResult(imageIndex, validLabel);
}

void ProofreadingController::updateUi(bool automationRunning)
{
    m_automationRunning = automationRunning;
    const bool active = isActive();
    const bool hasProject = m_callbacks.project && !m_callbacks.project().isEmpty();
    if (m_statusLabel)
        m_statusLabel->setVisible(active);
    if (m_toggleAction) {
        m_toggleAction->setText(active ? text("End Proofreading") : text("Start Proofreading Baseline"));
        m_toggleAction->setEnabled(!automationRunning && hasProject);
    }
    if (m_showChangesAction)
        m_showChangesAction->setEnabled(!automationRunning && hasProject && active);
    if (m_compareAction)
        m_compareAction->setEnabled(!automationRunning && hasProject);
}
