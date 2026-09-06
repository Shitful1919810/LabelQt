#pragma once

#include "services/project/ProjectController.h"
#include <QObject>
#include <QPointer>
#include <functional>

class QAction;
class QWidget;
class QPushButton;

class ProjectRecoveryController final : public QObject {
    Q_OBJECT
public:
    struct Callbacks {
        std::function<const labelqt::core::AppPreferences&()> preferences;
        std::function<bool()> blocked;
        std::function<bool()> prepareRecovery;
        std::function<void()> detachViews;
        std::function<void(bool)> refreshViews;
        std::function<bool()> saveCopy;
        std::function<bool(const QString&, std::optional<labelqt::services::FileRevision>)> reloadProject;
        std::function<void()> commitEditors;
    };
    ProjectRecoveryController(QWidget* window, labelqt::services::ProjectController& project, Callbacks callbacks,
                              QObject* parent = nullptr);
    QAction* action() const;
    QPushButton* notice() const;
    void check(const QString& projectPath);
    bool resolveSaveConflict(const labelqt::services::ProjectFileConflict& conflict);
    bool resolveDiskChange(std::optional<labelqt::services::FileRevision> revision, const QString& error = {});
    void open();

private:
    QPointer<QWidget> m_window;
    labelqt::services::ProjectController& m_project;
    Callbacks m_callbacks;
    QAction* m_action;
    QPointer<QPushButton> m_notice;
    QString m_recoveryPath;
};
