#pragma once

#include "services/io/FileRevision.h"
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <optional>

namespace labelqt::services {

// Observations never advance the loaded/saved baseline. Only the document owner can do that.
class ProjectFileMonitor final : public QObject {
    Q_OBJECT
public:
    explicit ProjectFileMonitor(QObject* parent = nullptr);
    ~ProjectFileMonitor() override;
    void setBaseline(const QString& path, const FileRevision& revision);
    void checkNow();
    bool hasExternalChange() const;
    const std::optional<FileRevision>& observedRevision() const
    {
        return m_observed;
    }
    QString error() const
    {
        return m_error;
    }
    quint64 generation() const
    {
        return m_generation;
    }

signals:
    void observationChanged();

private:
    void watchPaths();
    void scheduleCheck();
    QFileSystemWatcher m_files;
    QTimer m_debounce;
    QTimer m_poll;
    QString m_path;
    FileRevision m_baseline;
    std::optional<FileRevision> m_observed;
    QString m_error;
    quint64 m_generation{0};
    bool m_reading{false};
    bool m_pending{false};
};
} // namespace labelqt::services
