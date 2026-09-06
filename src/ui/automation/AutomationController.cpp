#include "ui/automation/AutomationController.h"
#include "services/automation/AutomationProtocol.h"

#include "core/RuntimePaths.h"
#include "ui/automation/AutomationParameterDialog.h"
#include "ui/automation/AutomationRunDialog.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <optional>
#include <utility>

AutomationController::AutomationController(QWidget* window, QObject* parent) : QObject(parent), m_window(window) {}

void AutomationController::setMenu(QMenu* menu)
{
    m_menu = menu;
    rebuildMenu();
}

void AutomationController::setPreferences(labelqt::core::AppPreferences preferences)
{
    m_preferences = std::move(preferences);
}

void AutomationController::setCallbacks(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

void AutomationController::refreshScripts()
{
    QStringList warnings;
    m_scripts = labelqt::services::AutomationService::discoverScripts(&warnings);
    emit scriptsChanged(m_scripts);
    if (!warnings.isEmpty()) {
        emit discoveryWarningsFound(warnings);
    }
    rebuildMenu();
}

const QVector<labelqt::services::AutomationScript>& AutomationController::scripts() const noexcept
{
    return m_scripts;
}

bool AutomationController::isRunning() const noexcept
{
    return m_running;
}

void AutomationController::runScriptById(const QString& scriptId)
{
    const auto script = std::find_if(m_scripts.cbegin(), m_scripts.cend(),
                                     [&scriptId](const auto& candidate) { return candidate.id == scriptId; });
    if (script == m_scripts.cend()) {
        showMissingScriptMessage(scriptId);
        return;
    }

    runScript(*script);
}

void AutomationController::showMissingScriptMessage(const QString& scriptId)
{
    emit statusMessageRequested(tr("Automation script is no longer available: %1").arg(scriptId), 5000);
}

void AutomationController::cancelRunningScript()
{
    if (m_taskService)
        (void)m_taskService->cancel(m_taskId);
    if (!m_runner.isNull()) {
        m_runner->cancel();
    }
    if (!m_runDialog.isNull()) {
        m_runDialog->setCanceling();
    }
    emit statusMessageRequested(tr("Canceling automation script..."), 0);
}

void AutomationController::rebuildMenu()
{
    if (m_menu == nullptr) {
        return;
    }

    m_menu->clear();
    m_cancelAction = m_menu->addAction(tr("Cancel Running Script"));
    m_cancelAction->setObjectName(QStringLiteral("automationCancelAction"));
    m_cancelAction->setEnabled(m_running);
    connect(m_cancelAction, &QAction::triggered, this, &AutomationController::cancelRunningScript);
    m_menu->addSeparator();

    if (m_scripts.isEmpty()) {
        QAction* emptyAction = m_menu->addAction(tr("No automation scripts found"));
        emptyAction->setObjectName(QStringLiteral("automationEmptyAction"));
        emptyAction->setEnabled(false);
    }
    else {
        QHash<QString, QVector<int>> scriptIndexesByDirectory;
        QStringList directoryOrder;
        for (int i = 0; i < m_scripts.size(); ++i) {
            const QString directoryPath = m_scripts.at(i).directoryPath;
            if (!scriptIndexesByDirectory.contains(directoryPath)) {
                directoryOrder.append(directoryPath);
            }
            scriptIndexesByDirectory[directoryPath].append(i);
        }

        auto addScriptAction = [this](QMenu* menu, int scriptIndex) {
            const labelqt::services::AutomationScript& script = m_scripts.at(scriptIndex);
            QAction* scriptAction = menu->addAction(script.name);
            scriptAction->setObjectName(QStringLiteral("automationScriptAction"));
            scriptAction->setData(script.id);
            scriptAction->setToolTip(script.description);
            scriptAction->setEnabled(!m_running);
            connect(scriptAction, &QAction::triggered, this, [this, scriptId = script.id]() {
                QTimer::singleShot(0, this, [this, scriptId]() { runScriptById(scriptId); });
            });
        };

        for (const QString& directoryPath : std::as_const(directoryOrder)) {
            const QVector<int> scriptIndexes = scriptIndexesByDirectory.value(directoryPath);
            if (scriptIndexes.size() == 1) {
                addScriptAction(m_menu, scriptIndexes.first());
                continue;
            }

            const labelqt::services::AutomationScript& firstScript = m_scripts.at(scriptIndexes.first());
            QMenu* scriptMenu = m_menu->addMenu(firstScript.directoryName);
            scriptMenu->menuAction()->setObjectName(QStringLiteral("automationScriptMenuAction"));
            scriptMenu->setEnabled(!m_running);
            for (int scriptIndex : scriptIndexes) {
                addScriptAction(scriptMenu, scriptIndex);
            }
        }
    }

    m_menu->addSeparator();
    QAction* openUserScriptFolderAction = m_menu->addAction(tr("Open User Script Folder"));
    openUserScriptFolderAction->setObjectName(QStringLiteral("automationOpenUserScriptFolderAction"));
    connect(openUserScriptFolderAction, &QAction::triggered, this, &AutomationController::openUserScriptFolder);

    QAction* refreshAction = m_menu->addAction(tr("Refresh Scripts"));
    refreshAction->setObjectName(QStringLiteral("automationRefreshAction"));
    refreshAction->setEnabled(!m_running);
    connect(refreshAction, &QAction::triggered, this,
            [this]() { QTimer::singleShot(0, this, &AutomationController::refreshScripts); });
}

AutomationController::~AutomationController()
{
    if (m_runner)
        QObject::disconnect(m_runner, nullptr, this, nullptr);
    if (m_runDialog)
        QObject::disconnect(m_runDialog, nullptr, this, nullptr);
    if (m_taskService)
        (void)m_taskService->cancel(m_taskId);
}

void AutomationController::updateMenuEnabledState()
{
    if (m_menu == nullptr) {
        return;
    }

    const auto updateActions = [this](const QList<QAction*>& actions, const auto& updateActionsRef) -> void {
        for (QAction* action : actions) {
            if (action == nullptr || action->isSeparator()) {
                continue;
            }

            if (action == m_cancelAction || action->objectName() == QStringLiteral("automationCancelAction")) {
                action->setEnabled(m_running);
            }
            else if (action->objectName() == QStringLiteral("automationEmptyAction")) {
                action->setEnabled(false);
            }
            else if (action->objectName() == QStringLiteral("automationOpenUserScriptFolderAction")) {
                action->setEnabled(true);
            }
            else {
                action->setEnabled(!m_running);
            }

            if (QMenu* menu = action->menu()) {
                updateActionsRef(menu->actions(), updateActionsRef);
            }
        }
    };
    updateActions(m_menu->actions(), updateActions);
}

void AutomationController::runScript(const labelqt::services::AutomationScript& script)
{
    if (!QFileInfo::exists(script.entryPath)) {
        showMissingScriptMessage(script.id);
        QTimer::singleShot(0, this, &AutomationController::refreshScripts);
        return;
    }
    if (m_running) {
        return;
    }
    if (m_callbacks.isProjectEmpty == nullptr || m_callbacks.project == nullptr || m_callbacks.isProjectEmpty()) {
        QMessageBox::information(m_window.data(), tr("Automation"),
                                 tr("Open a project before running automation scripts."));
        return;
    }

    if (m_callbacks.commitActiveTextInput != nullptr) {
        m_callbacks.commitActiveTextInput();
    }
    const QStringList groups = m_callbacks.groups == nullptr ? QStringList{} : m_callbacks.groups();
    const QVector<labelqt::core::LabelGroupStyle> groupStyles =
        m_callbacks.groupStyles == nullptr ? QVector<labelqt::core::LabelGroupStyle>{} : m_callbacks.groupStyles();
    const std::optional<AutomationParameterDialog::Values> values =
        AutomationParameterDialog::getValues(m_window.data(), script, groups, groupStyles);
    if (!values.has_value()) {
        return;
    }

    QString automationError;
    if (!labelqt::services::AutomationService::storeParameterSecrets(script, values->secrets, &automationError)) {
        QMessageBox::warning(m_window.data(), tr("Automation"), automationError);
        return;
    }

    QMap<QString, QString> secretEnvironment;
    if (!labelqt::services::AutomationService::secretEnvironment(script, &secretEnvironment, &automationError)) {
        QMessageBox::warning(m_window.data(), tr("Automation"), automationError);
        return;
    }

    if (!m_taskService)
        return;
    if (m_callbacks.commitActiveTextInput)
        m_callbacks.commitActiveTextInput();
    const int imageIndex = m_callbacks.currentImageIndex == nullptr ? -1 : m_callbacks.currentImageIndex();
    const auto selectedLabels = m_callbacks.selectedLabels ? m_callbacks.selectedLabels() : QVector<int>{};
    const auto request =
        labelqt::services::AutomationProtocol::plan(script.task, m_callbacks.project(), imageIndex, selectedLabels);
    if (!request) {
        QMessageBox::warning(m_window.data(), tr("Automation failed"),
                             tr("Select the pages or labels required by this script."));
        return;
    }
    const auto task = m_taskService->capture(*request);
    if (!task) {
        QMessageBox::warning(m_window.data(), tr("Automation failed"), tr("The automation script failed."));
        return;
    }
    QJsonObject selection;
    if (m_callbacks.selection && imageIndex >= 0 && imageIndex < m_callbacks.project().images().size()) {
        const auto selected = m_callbacks.selection();
        const auto pageId = m_callbacks.project().images().at(imageIndex).id;
        if (selected && task->pages.contains(pageId)) {
            const auto rect = selected->normalized().intersected(QRectF(0, 0, 1, 1));
            if (!rect.isEmpty())
                selection = {{"pageId", pageId.toString(QUuid::WithoutBraces)},
                             {"x", rect.x()},
                             {"y", rect.y()},
                             {"width", rect.width()},
                             {"height", rect.height()}};
        }
    }
    m_taskId = task->taskId;
    emit statusMessageRequested(tr("Running automation script: %1").arg(script.name), 0);

    auto* runner = new labelqt::services::AutomationRunner(this);
    const bool willInstallRequirements =
        m_preferences.automationAutoInstallRequirements() &&
        QFileInfo::exists(QDir(script.directoryPath).filePath(QStringLiteral("requirements.txt")));
    AutomationRunDialog* dialog = nullptr;
    if (m_preferences.showAutomationRunLog() || willInstallRequirements) {
        dialog = new AutomationRunDialog(script.name, m_window.data());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_runner = runner;
    m_runDialog = dialog;

    if (dialog != nullptr) {
        connect(dialog, &AutomationRunDialog::cancelRequested, this, &AutomationController::cancelRunningScript);
        connect(dialog, &QObject::destroyed, this, [this]() { m_runDialog.clear(); });
        connect(runner, &labelqt::services::AutomationRunner::standardOutputReceived, dialog,
                &AutomationRunDialog::appendStandardOutput);
        connect(runner, &labelqt::services::AutomationRunner::standardErrorReceived, dialog,
                &AutomationRunDialog::appendStandardError);
    }
    connect(runner, &labelqt::services::AutomationRunner::finished, this,
            [this, runner, dialog = QPointer<AutomationRunDialog>(dialog),
             script](const labelqt::services::AutomationRunResult& result) {
                finishScript(runner, dialog, script, result);
            });

    setRunning(true);
    if (dialog != nullptr) {
        dialog->show();
    }

    runner->start(script, *task, values->parameters, selection, secretEnvironment,
                  {
                      m_preferences.automationPythonCommand(),
                      m_preferences.automationPythonArguments(),
                      m_preferences.automationAutoInstallRequirements(),
                      m_preferences.automationPipIndexUrl(),
                  });
}

void AutomationController::openUserScriptFolder()
{
    const QString path = labelqt::core::userAutomationScriptsDirectory();
    if (path.isEmpty()) {
        QMessageBox::warning(m_window.data(), tr("Automation"), tr("Could not open the user script folder."));
        return;
    }

    QDir directory(path);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        QMessageBox::warning(m_window.data(), tr("Automation"), tr("Could not open the user script folder."));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()))) {
        QMessageBox::warning(m_window.data(), tr("Automation"), tr("Could not open the user script folder."));
    }
}

void AutomationController::finishScript(labelqt::services::AutomationRunner* runner,
                                        QPointer<AutomationRunDialog> dialog,
                                        labelqt::services::AutomationScript script,
                                        const labelqt::services::AutomationRunResult& result)
{
    if (m_runner == runner) {
        m_runner.clear();
    }
    const QPointer<AutomationRunDialog> dialogPointer = dialog;
    runner->deleteLater();
    QTimer::singleShot(
        0, this, [this, dialogPointer, script, result = labelqt::services::AutomationRunResult(result)]() mutable {
            if (result.success) {
                const auto committed =
                    m_taskService && result.output.taskId == m_taskId
                        ? m_taskService->submit(m_taskId, result.output.changes,
                                                tr("Running automation script: %1").arg(script.name))
                        : std::expected<labelqt::services::ProjectTaskCommitReceipt, labelqt::services::TaskError>(
                              std::unexpected(labelqt::services::TaskError::InvalidChange));
                if (!committed) {
                    result.success = false;
                    using labelqt::services::TaskError;
                    if (committed.error() == TaskError::Conflict || committed.error() == TaskError::StaleSession)
                        result.error = tr("The project changed while the script was running. No changes were applied.");
                    else if (committed.error() == TaskError::UnknownTask)
                        result.error = tr("Automation script was canceled.");
                    else
                        result.error =
                            tr("The script returned invalid or unauthorized changes. No changes were applied.");
                }
            }
            if (m_taskService)
                (void)m_taskService->cancel(m_taskId);
            m_taskId = {};
            if (!dialogPointer.isNull()) {
                dialogPointer->setFinished(result.success);
            }

            if (!result.success) {
                setRunning(false);
                const QString errorMessage = result.error == QStringLiteral("Automation script was canceled.")
                                                 ? tr("Automation script was canceled.")
                                                 : result.error;
                const QString displayError =
                    errorMessage.isEmpty() ? tr("The automation script failed.") : errorMessage;
                if (!dialogPointer.isNull()) {
                    dialogPointer->appendStandardError(displayError + QLatin1Char('\n'));
                }
                else {
                    QMessageBox::critical(m_window.data(), tr("Automation failed"), displayError);
                }
                emit statusMessageRequested(tr("Automation script failed: %1").arg(script.name), 5000);
                if (!dialogPointer.isNull() && !dialogPointer->isVisible()) {
                    dialogPointer->deleteLater();
                }
                return;
            }

            setRunning(false);

            if (!result.output.quiet) {
                const QString title = result.output.title.isEmpty() ? script.name : result.output.title;
                const QString text = result.output.text.isEmpty() ? result.output.summary : result.output.text;
                QMessageBox::information(!dialogPointer.isNull() && dialogPointer->isVisible()
                                             ? static_cast<QWidget*>(dialogPointer.data())
                                             : m_window.data(),
                                         title, text);
            }
            emit statusMessageRequested(tr("Automation script finished: %1").arg(script.name), 5000);
            if (!dialogPointer.isNull() && !dialogPointer->isVisible()) {
                dialogPointer->deleteLater();
            }
        });
}

void AutomationController::setRunning(bool running)
{
    m_running = running;
    emit runningChanged(running);
    updateMenuEnabledState();
}
