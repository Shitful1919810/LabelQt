#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/automation/AutomationService.h"
#include "services/automation/ProjectTaskService.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

class AutomationRunDialog;
class QAction;
class QMenu;
class QWidget;

class AutomationController final : public QObject {
    Q_OBJECT

public:
    struct Callbacks {
        std::function<bool()> isProjectEmpty;
        std::function<void()> commitActiveTextInput;
        std::function<const labelqt::core::Project&()> project;
        std::function<QStringList()> groups;
        std::function<QVector<labelqt::core::LabelGroupStyle>()> groupStyles;
        std::function<int()> currentImageIndex;
        std::function<std::optional<QRectF>()> selection;
        std::function<QVector<int>()> selectedLabels;
    };

    explicit AutomationController(QWidget* window, QObject* parent = nullptr);
    ~AutomationController() override;

    void setMenu(QMenu* menu);
    void setPreferences(labelqt::core::AppPreferences preferences);
    void setCallbacks(Callbacks callbacks);
    void setTaskService(labelqt::services::ProjectTaskService* service)
    {
        m_taskService = service;
    }
    void refreshScripts();
    const QVector<labelqt::services::AutomationScript>& scripts() const noexcept;
    bool isRunning() const noexcept;

public slots:
    void runScriptById(const QString& scriptId);
    void showMissingScriptMessage(const QString& scriptId);
    void cancelRunningScript();

signals:
    void scriptsChanged(const QVector<labelqt::services::AutomationScript>& scripts);
    void discoveryWarningsFound(const QStringList& warnings);
    void runningChanged(bool running);
    void statusMessageRequested(const QString& message, int timeoutMs);

private:
    struct Run;
    void rebuildMenu();
    void updateMenuEnabledState();
    void runScript(labelqt::services::AutomationScript script);
    void openUserScriptFolder();
    void finishScript(const std::shared_ptr<Run>& run, const labelqt::services::AutomationRunResult& result);
    void finishRun(const std::shared_ptr<Run>& run);

    QPointer<QWidget> m_window;
    QMenu* m_menu{nullptr};
    QAction* m_cancelAction{nullptr};
    Callbacks m_callbacks;
    labelqt::core::AppPreferences m_preferences;
    QVector<labelqt::services::AutomationScript> m_scripts;
    std::shared_ptr<Run> m_run;
    QPointer<labelqt::services::ProjectTaskService> m_taskService;
};
