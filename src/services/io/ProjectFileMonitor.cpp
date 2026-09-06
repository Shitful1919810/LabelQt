#include "services/io/ProjectFileMonitor.h"
#include "core/LabelPlusDocument.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace labelqt::services {
namespace {
struct Observation {
    std::optional<FileRevision> revision;
    QString error;
    bool unstable{false};
};
Observation readStableRevision(const QString& path, const FileRevision& baseline, const Observation& previous)
{
    try {
        const auto before = FileRevision::read(path);
        if (!before)
            return {{}, before.error()};
        QString parseError;
        if (before->exists && !before->sameContent(baseline)) {
            if (previous.revision && previous.revision->sameContent(*before) && previous.error.isEmpty()) {
                parseError = previous.error;
            }
            else {
                try {
                    const auto loaded = labelqt::core::LabelPlusDocument::readFromFile(path);
                    if (loaded.sha256 != before->sha256)
                        return {{}, {}, true};
                }
                catch (const std::exception& error) {
                    parseError = QString::fromUtf8(error.what());
                }
            }
        }
        const auto after = FileRevision::read(path);
        if (!after)
            return {{}, after.error()};
        if (!before->sameContent(*after))
            return {{}, {}, true};
        return {*after, parseError};
    }
    catch (const std::exception& error) {
        return {{}, QString::fromUtf8(error.what())};
    }
}
} // namespace

ProjectFileMonitor::ProjectFileMonitor(QObject* parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    m_poll.setInterval(5000);
    connect(&m_files, &QFileSystemWatcher::fileChanged, this, &ProjectFileMonitor::scheduleCheck);
    connect(&m_files, &QFileSystemWatcher::directoryChanged, this, &ProjectFileMonitor::scheduleCheck);
    connect(&m_debounce, &QTimer::timeout, this, &ProjectFileMonitor::checkNow);
    connect(&m_poll, &QTimer::timeout, this, &ProjectFileMonitor::checkNow);
}

ProjectFileMonitor::~ProjectFileMonitor()
{
    for (QObject* child : children())
        disconnect(child, nullptr, this, nullptr);
}

void ProjectFileMonitor::setBaseline(const QString& path, const FileRevision& revision)
{
    ++m_generation;
    m_debounce.stop();
    m_poll.stop();
    if (!m_files.files().isEmpty())
        m_files.removePaths(m_files.files());
    if (!m_files.directories().isEmpty())
        m_files.removePaths(m_files.directories());
    // Keep the logical filename: atomic replacement can change its canonical target.
    m_path = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
    m_baseline = revision;
    m_observed.reset();
    m_error.clear();
    m_pending = false;
    if (!m_path.isEmpty()) {
        watchPaths();
        m_poll.start();
        scheduleCheck();
    }
    emit observationChanged();
}

bool ProjectFileMonitor::hasExternalChange() const
{
    return !m_error.isEmpty() || (m_observed && !m_observed->sameContent(m_baseline));
}

void ProjectFileMonitor::watchPaths()
{
    if (m_path.isEmpty())
        return;
    const QFileInfo file(m_path);
    if (file.exists() && !m_files.files().contains(m_path))
        m_files.addPath(m_path);
    if (!m_files.directories().contains(file.absolutePath()))
        m_files.addPath(file.absolutePath());
}

void ProjectFileMonitor::scheduleCheck()
{
    m_debounce.start();
}

void ProjectFileMonitor::checkNow()
{
    if (m_path.isEmpty())
        return;
    if (m_reading) {
        m_pending = true;
        return;
    }
    m_debounce.stop();
    watchPaths();
    m_reading = true;
    m_pending = false;
    auto* watcher = new QFutureWatcher<Observation>(this);
    connect(watcher, &QFutureWatcher<Observation>::finished, this, [this, watcher, generation = m_generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_reading = false;
        if (m_pending || result.unstable || generation != m_generation)
            scheduleCheck();
        if (generation == m_generation && !result.unstable) {
            const bool changed = m_error != result.error || m_observed.has_value() != result.revision.has_value() ||
                                 (m_observed && result.revision && !m_observed->sameContent(*result.revision));
            m_observed = result.revision;
            m_error = result.error;
            if (changed)
                emit observationChanged();
        }
    });
    // The worker does not access the service or the live project.
    watcher->setFuture(QtConcurrent::run(readStableRevision, m_path, m_baseline, Observation{m_observed, m_error}));
}
} // namespace labelqt::services
