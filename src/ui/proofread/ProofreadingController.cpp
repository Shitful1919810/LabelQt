#include "ui/proofread/ProofreadingController.h"

#include "core/LabelPlusDocument.h"
#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ReviewMetadataService.h"
#include "ui/labeltable/LabelTableModel.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include "ui/widgets/BackgroundTask.h"
#include "ui/widgets/BusyTaskDialog.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
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
};

struct ComparisonTaskResult {
    labelqt::services::ProjectComparisonPlan plan;
    QVector<labelqt::services::ReviewChange> changes;
};

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

template <typename Work>
auto runComparisonTask(QWidget* parent, Work work) -> std::optional<std::invoke_result_t<Work>>
{
    using Result = std::invoke_result_t<Work>;
    auto future = QtConcurrent::run([work = std::move(work)]() -> std::expected<Result, QString> {
        try {
            return work();
        }
        catch (const std::exception& error) {
            return std::unexpected(QString::fromUtf8(error.what()));
        }
        catch (...) {
            return std::unexpected(text("Unknown error"));
        }
    });
    labelqt::ui::BusyTaskDialog dialog(text("Comparing projects..."), text("Abort"), parent);
    auto result = labelqt::ui::waitForBackgroundTask(std::move(future), dialog);
    if (!result)
        return std::nullopt;
    if (!*result) {
        warning(parent, text("Compare Projects"), text("Failed to compare projects: %1").arg(result->error()));
        return std::nullopt;
    }
    return std::move(**result);
}

std::optional<ComparisonTaskResult> runComparison(QWidget* parent, labelqt::core::Project baseline,
                                                  labelqt::core::Project current,
                                                  labelqt::services::ProjectPageMatchMode mode)
{
    return runComparisonTask(parent, [baseline = std::move(baseline), current = std::move(current), mode] {
        auto plan = labelqt::services::ProjectComparisonService::planComparison(baseline, current, mode);
        auto changes = labelqt::services::ProjectComparisonService::changesBetweenProjects(current, plan);
        return ComparisonTaskResult{std::move(plan), std::move(changes)};
    });
}
} // namespace

ProofreadingController::ProofreadingController(QWidget* parentWidget,
                                               labelqt::services::ProjectController& projectController,
                                               labelqt::core::UndoStack& undoStack, Callbacks callbacks,
                                               QObject* parent)
    : QObject(parent), m_parentWidget(parentWidget), m_projectController(projectController), m_undoStack(undoStack),
      m_callbacks(std::move(callbacks))
{
}

void ProofreadingController::setUi(QAction* toggleAction, QAction* showChangesAction, QAction* compareAction,
                                   QLabel* statusLabel, QMenu* menu, ProofreadNotesWidget* notes,
                                   LabelTableModel* labelModel)
{
    m_toggleAction = toggleAction;
    m_showChangesAction = showChangesAction;
    m_compareAction = compareAction;
    m_statusLabel = statusLabel;
    m_notes = notes;
    m_labelModel = labelModel;
    m_modeAction = menu->addAction(tr("Revision interactions"));
    m_modeAction->setCheckable(true);
    m_modeAction->setChecked(true);
    connect(m_modeAction, &QAction::toggled, this, [this] { updateUi(m_automationRunning); });
    m_clearAction = menu->addAction(tr("Clear proofreading record..."), this, &ProofreadingController::clearRound);
    connect(notes, &ProofreadNotesWidget::requested, this, &ProofreadingController::editNote);
}

bool ProofreadingController::loadRound()
{
    const auto& project = m_callbacks.project();
    if (m_cacheValid && m_cachedDocumentSession == m_projectController.documentSession() &&
        project.commentLines() == m_cachedComments && project.filePath() == m_cachedPath)
        return m_recordError.isEmpty();
    m_cacheValid = true;
    m_cachedComments = project.commentLines();
    m_cachedPath = project.filePath();
    m_cachedDocumentSession = m_projectController.documentSession();
    const auto round = labelqt::services::ReviewRoundService::read(project);
    m_recordError = round ? QString() : round.error();
    m_round = round ? *round : std::nullopt;
    return round.has_value();
}

bool ProofreadingController::isActive() const
{
    return m_round && !m_round->after && m_modeAction && m_modeAction->isChecked();
}

bool ProofreadingController::commitRound(const std::optional<labelqt::services::ReviewRound>& round,
                                         labelqt::services::ReviewEditToken token, const QString& description)
{
    const auto result =
        labelqt::services::ReviewRoundEditService::commit(m_projectController, m_undoStack, token, round, description);
    if (!result) {
        warning(m_parentWidget, tr("Proofreading"), result.error());
        return false;
    }
    m_callbacks.markDirty();
    updateUi(m_automationRunning);
    return true;
}

void ProofreadingController::toggleBaseline()
{
    using namespace labelqt::services;
    if (m_callbacks.isEditingBlocked && m_callbacks.isEditingBlocked())
        return;
    m_callbacks.commitActiveTextInput();
    if (!loadRound()) {
        warning(m_parentWidget, tr("Proofreading"), m_recordError);
        return;
    }
    const auto& project = m_callbacks.project();
    if (project.isEmpty() && !m_round)
        return;
    const auto token = ReviewRoundEditService::token(m_projectController);
    const auto previous = m_round;
    if (m_round && !m_round->after) {
        if (messageBox(m_parentWidget, QMessageBox::Question, tr("Complete proofreading"),
                       tr("Freeze this round's original draft, final draft and comments? Later edits will not change "
                          "its text differences."),
                       QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        auto completed = ReviewRoundService::complete(*previous, project);
        commitRound(completed, token, tr("Complete proofreading"));
        return;
    }
    if (m_round && messageBox(m_parentWidget, QMessageBox::Warning, tr("Start next proofreading round"),
                              tr("This replaces the previous round. Export its report first if you need to keep it."),
                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
        return;
    ReviewRound round;
    round.before = ReviewRoundService::snapshot(project);
    if (commitRound(round, token, tr("Start proofreading")))
        m_modeAction->setChecked(true);
}

void ProofreadingController::clearRound()
{
    using namespace labelqt::services;
    if (m_callbacks.isEditingBlocked && m_callbacks.isEditingBlocked())
        return;
    m_callbacks.commitActiveTextInput();
    if (!loadRound() || !m_round)
        return;
    const auto token = ReviewRoundEditService::token(m_projectController);
    if (messageBox(
            m_parentWidget, QMessageBox::Warning, tr("Clear proofreading record"),
            tr("Remove this round's snapshots, comments and review states? The current translation will be kept."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok)
        commitRound(std::nullopt, token, tr("Clear proofreading record"));
}

void ProofreadingController::showChanges()
{
    using namespace labelqt::services;
    m_callbacks.commitActiveTextInput();
    if (!loadRound()) {
        warning(m_parentWidget, tr("Proofreading"), m_recordError);
        return;
    }
    if (!m_round)
        return;
    const auto token = ReviewRoundEditService::token(m_projectController);
    auto round = *m_round;
    auto result =
        runComparisonTask(m_parentWidget, [round, live = ReviewRoundService::snapshot(m_callbacks.project())] {
            return ReviewRoundService::present(round, live);
        });
    if (!result || ReviewRoundEditService::token(m_projectController).session != token.session)
        return;
    auto presentation = std::move(*result);
    if (presentation.uncertainIdentity)
        information(m_parentWidget, tr("Proofreading"),
                    tr("Some label identities changed outside LabelQt. Differences use estimated matching; comments "
                       "are never reassigned automatically."));
    presentation.after.setCommentLines(ReviewSessionService::withProjectSession({}, round.session));
    ProofreadChangesDialog dialog(presentation.before, presentation.after, m_callbacks.preferences(), {},
                                  std::move(presentation.changes),
                                  {.leftProjectTitle = tr("Original translation"),
                                   .rightProjectTitle = tr("Reviewed translation"),
                                   .reviewEnabled = true,
                                   .projectReviewSession = true,
                                   .roundReview = true},
                                  m_parentWidget);
    const int dialogResult = dialog.exec();
    const auto original = ReviewSessionService::decode(round.session);
    if (original && dialog.reviewSession() != ReviewSessionService::encode(*original)) {
        round.session = dialog.reviewSession();
        if (!commitRound(round, token, tr("Edit review session")))
            return;
    }
    if (ReviewRoundEditService::token(m_projectController).session != token.session)
        return;
    if (dialogResult == QDialog::Accepted) {
        const auto* change = dialog.selectedChange();
        if (!change)
            return;
        const QUuid label = !change->current.id.isNull() ? change->current.id : change->baseline.id;
        const auto target = ReviewRoundService::locate(m_callbacks.project(), change->pageId, label);
        selectDialogResult(target.first, target.second);
    }
}

void ProofreadingController::editNote(ProofreadNotesWidget::Operation operation, QUuid id)
{
    using namespace labelqt::services;
    using Operation = ProofreadNotesWidget::Operation;
    if (m_callbacks.isEditingBlocked && m_callbacks.isEditingBlocked())
        return;
    m_callbacks.commitActiveTextInput();
    if (!loadRound() || !m_round)
        return;
    auto round = *m_round;
    const auto token = ReviewRoundEditService::token(m_projectController);
    const auto selected = m_callbacks.currentSelection();
    const auto& project = m_callbacks.project();
    const auto it = std::find_if(round.notes.cbegin(), round.notes.cend(), [&](const auto& note) { return note.id == id; });
    if (operation == Operation::Jump) {
        if (it != round.notes.end()) {
            const auto target = ReviewRoundService::locateNote(project, *it);
            selectDialogResult(target.first, target.second);
        }
        return;
    }
    if (round.after && operation != Operation::Resolve && operation != Operation::Relink)
        return;
    ReviewNoteEdit edit{.operation = ReviewNoteEdit::Operation::Edit, .noteId = id};
    const bool adding = operation == Operation::AddLabel || operation == Operation::AddPage;
    if (adding || operation == Operation::Relink) {
        if (selected.first < 0 || selected.first >= project.images().size())
            return;
        const auto& page = project.images().at(selected.first);
        edit.pageId = page.id;
        if (operation != Operation::AddPage) {
            if (selected.second < 0 || selected.second >= page.labels.size() ||
                page.labels.at(selected.second).isDeleted())
                return;
            edit.labelId = page.labels.at(selected.second).id();
        }
    }
    if (!adding && it == round.notes.cend())
        return;
    if (operation == Operation::Resolve) {
        edit.operation = ReviewNoteEdit::Operation::Resolve;
        edit.resolved = !it->resolved;
    }
    else if (operation == Operation::Remove) {
        if (messageBox(m_parentWidget, QMessageBox::Question, tr("Delete comment"), tr("Delete this comment?"),
                       QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        edit.operation = ReviewNoteEdit::Operation::Remove;
    }
    else if (operation == Operation::Relink) {
        if (messageBox(m_parentWidget, QMessageBox::Question, tr("Attach comment"),
                       tr("Attach this comment to the currently selected label? Its original quote will be preserved."),
                       QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        edit.operation = ReviewNoteEdit::Operation::Relink;
    }
    else {
        bool ok = false;
        const QString value =
            QInputDialog::getMultiLineText(m_parentWidget, tr("Proofreading comment"), tr("Comment"),
                                          adding ? QString{} : it->text, &ok);
        if (!ok || value.trimmed().isEmpty())
            return;
        edit.operation = adding ? ReviewNoteEdit::Operation::Add : ReviewNoteEdit::Operation::Edit;
        edit.text = value;
    }
    const auto result = ReviewRoundEditService::editNote(round, m_callbacks.project(), edit);
    if (!result) {
        warning(m_parentWidget, tr("Proofreading comment"), result.error());
        return;
    }
    commitRound(round, token, tr("Edit proofreading comments"));
}

void ProofreadingController::compareWithProject()
{
    if (m_callbacks.commitActiveTextInput)
        m_callbacks.commitActiveTextInput();
    auto& project = m_callbacks.project();
    const auto documentSession = m_projectController.documentSession();
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
    if (m_projectController.documentSession() != documentSession)
        return;
    const auto current = labelqt::services::ReviewRoundService::snapshot(project);
    auto result = runComparison(m_parentWidget, current, target, labelqt::services::ProjectPageMatchMode::ByName);
    if (!result)
        return;
    if (labelqt::services::ProjectComparisonService::shouldOfferOrderPageMatching(result->plan) &&
        messageBox(m_parentWidget, QMessageBox::Question, text("Compare Projects"),
                   text("The selected project has very few page names in common with the current project, but both "
                        "projects have the same number of pages. Compare pages by their current order instead?"),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        result = runComparison(m_parentWidget, current, target, labelqt::services::ProjectPageMatchMode::ByOrder);
        if (!result)
            return;
    }
    if (result->changes.isEmpty()) {
        information(m_parentWidget, text("Compare Projects"), text("No differences from the selected project."));
        return;
    }
    ProofreadChangesDialog dialog(current, target, m_callbacks.preferences(), std::move(result->plan),
                                  std::move(result->changes),
                                  {.leftProjectTitle = text("Current Project"),
                                   .rightProjectTitle = text("Target Project"),
                                   .jumpToLeftProject = true,
                                   .reviewEnabled = true},
                                  m_parentWidget);
    if (dialog.exec() == QDialog::Accepted && m_projectController.documentSession() == documentSession) {
        const int pageIndex = dialog.selectedImageIndex();
        if (pageIndex < 0 || pageIndex >= current.images().size())
            return;
        const auto& page = current.images().at(pageIndex);
        const int labelIndex = dialog.selectedLabelIndex();
        const auto labelId =
            labelIndex >= 0 && labelIndex < page.labels.size() ? page.labels.at(labelIndex).id() : QUuid{};
        const auto target = labelqt::services::ReviewRoundService::locate(project, page.id, labelId);
        selectDialogResult(target.first, target.second);
    }
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
    loadRound();
    const bool hasProject = !m_callbacks.project().isEmpty();
    const bool enabled = !automationRunning && (hasProject || m_round) && m_recordError.isEmpty();
    if (m_statusLabel) {
        m_statusLabel->setVisible(m_round.has_value() || !m_recordError.isEmpty());
        m_statusLabel->setText(!m_recordError.isEmpty()    ? tr("Proofreading record unavailable")
                               : m_round && m_round->after ? tr("Proofreading complete")
                               : isActive()                ? tr("Revision mode")
                                                           : tr("Proofreading in progress"));
        m_statusLabel->setToolTip(m_recordError.isEmpty() ? tr("Proofreading records are saved with the project.")
                                                          : m_recordError);
    }
    if (m_toggleAction) {
        m_toggleAction->setText(m_round && !m_round->after ? tr("Complete proofreading...")
                                : m_round                  ? tr("Start next proofreading round...")
                                                           : tr("Start proofreading"));
        m_toggleAction->setEnabled(enabled);
    }
    if (m_showChangesAction)
        m_showChangesAction->setEnabled(!automationRunning && (m_round || !m_recordError.isEmpty()));
    if (m_compareAction)
        m_compareAction->setEnabled(!automationRunning && hasProject);
    if (m_clearAction)
        m_clearAction->setEnabled(enabled && m_round.has_value());
    if (m_modeAction)
        m_modeAction->setEnabled(enabled && m_round && !m_round->after);
    if (m_notes) {
        const auto selection = m_callbacks.currentSelection();
        m_notes->refresh(m_round, m_callbacks.project(), selection.first, selection.second, enabled);
    }
    if (m_labelModel) {
        QHash<QUuid, bool> states;
        if (m_round)
            for (const auto& note : m_round->notes) {
                const auto id = note.targetLabelId();
                if (!id.isNull())
                    states[id] = states.value(id, true) && note.resolved;
            }
        m_labelModel->setCommentStates(std::move(states));
    }
}
