#pragma once

#include "services/io/ProjectFileMonitor.h"
#include "services/project/ProjectController.h"
#include <QPointer>
#include <functional>

class QPushButton;
class QWidget;

class ProjectExternalChangeController final : public QObject {
    Q_OBJECT
public:
    struct Callbacks {
        std::function<bool()> blocked;
        std::function<void(std::optional<labelqt::services::FileRevision>, const QString&)> resolve;
    };
    ProjectExternalChangeController(QWidget* window, labelqt::services::ProjectController& project, Callbacks callbacks,
                                    QObject* parent = nullptr);
    ~ProjectExternalChangeController() override;
    QPushButton* notice() const;

private:
    void offer(bool explicitRequest = false);
    QByteArray observationKey() const;
    QPointer<QWidget> m_window;
    QPointer<QPushButton> m_notice;
    labelqt::services::ProjectFileMonitor m_monitor;
    Callbacks m_callbacks;
    QTimer m_deferred;
    QByteArray m_acknowledged;
    bool m_presenting{false};
};
