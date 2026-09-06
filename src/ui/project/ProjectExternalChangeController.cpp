#include "ui/project/ProjectExternalChangeController.h"
#include <QApplication>
#include <QPushButton>
#include <QScopeGuard>

using namespace labelqt::services;

ProjectExternalChangeController::ProjectExternalChangeController(QWidget* window, ProjectController& project,
                                                                 Callbacks callbacks, QObject* parent)
    : QObject(parent), m_window(window), m_notice(new QPushButton(tr("Project changed on disk..."), window)),
      m_callbacks(std::move(callbacks))
{
    m_notice->hide();
    connect(m_notice, &QPushButton::clicked, this, [this] { offer(true); });
    connect(&m_monitor, &ProjectFileMonitor::observationChanged, this, [this] {
        if (m_notice)
            m_notice->setVisible(m_monitor.hasExternalChange());
        if (!m_monitor.hasExternalChange())
            m_acknowledged.clear();
    });
    connect(window, &QObject::destroyed, this, [this] {
        m_deferred.stop();
        m_monitor.setBaseline({}, {});
    });
    connect(&project, &ProjectController::storageChanged, this, [this, &project] {
        if (m_window)
            m_monitor.setBaseline(project.project().filePath(), project.savedRevision());
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            m_monitor.checkNow();
    });
    // Never open a dialog directly from a filesystem signal or a document mutation stack.
    m_deferred.setInterval(1000);
    connect(&m_deferred, &QTimer::timeout, this, [this] { offer(); });
    m_deferred.start();
    m_monitor.setBaseline(project.project().filePath(), project.savedRevision());
}

QPushButton* ProjectExternalChangeController::notice() const
{
    return m_notice;
}

ProjectExternalChangeController::~ProjectExternalChangeController()
{
    delete m_notice.data();
}

QByteArray ProjectExternalChangeController::observationKey() const
{
    const auto& revision = m_monitor.observedRevision();
    return QByteArray::number(m_monitor.generation()) + ':' +
           (revision ? revision->exists ? revision->sha256.toHex() : QByteArray("missing") : QByteArray("unreadable")) +
           ':' + m_monitor.error().toUtf8();
}

void ProjectExternalChangeController::offer(bool explicitRequest)
{
    if (m_presenting || !m_window || !m_window->isVisible() || !m_monitor.hasExternalChange() ||
        m_callbacks.blocked() || QApplication::activeModalWidget() || QApplication::activePopupWidget() ||
        (!explicitRequest && !m_window->isActiveWindow()))
        return;
    const auto key = observationKey();
    if (!explicitRequest && key == m_acknowledged)
        return;
    m_acknowledged = key;
    m_presenting = true;
    const auto guard = qScopeGuard([self = QPointer<ProjectExternalChangeController>(this)] {
        if (self) {
            self->m_presenting = false;
            self->m_monitor.checkNow();
        }
    });
    const auto resolve = m_callbacks.resolve;
    resolve(m_monitor.observedRevision(), m_monitor.error());
}
