#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/AutomationService.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

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
        std::function<labelqt::services::AutomationSelection()> selection;
        std::function<labelqt::services::AutomationContext()> context;
    };

    explicit AutomationController(QWidget* window, QObject* parent = nullptr);

    void setMenu(QMenu* menu);
    void setPreferences(labelqt::core::AppPreferences preferences);
    void setCallbacks(Callbacks callbacks);
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
    void operationsReady(const QString& scriptName, const QVector<labelqt::services::AutomationOperation>& operations);
    void statusMessageRequested(const QString& message, int timeoutMs);

private:
    void rebuildMenu();
    void updateMenuEnabledState();
    void runScript(const labelqt::services::AutomationScript& script);
    void finishScript(labelqt::services::AutomationRunner* runner, AutomationRunDialog* dialog,
                      labelqt::services::AutomationScript script, const labelqt::services::AutomationRunResult& result);
    void setRunning(bool running);

    QPointer<QWidget> m_window;
    QMenu* m_menu{nullptr};
    QAction* m_cancelAction{nullptr};
    Callbacks m_callbacks;
    labelqt::core::AppPreferences m_preferences;
    QVector<labelqt::services::AutomationScript> m_scripts;
    QPointer<labelqt::services::AutomationRunner> m_runner;
    QPointer<AutomationRunDialog> m_runDialog;
    bool m_running{false};
};
