#pragma once

#include "services/io/FileRevision.h"
#include "services/label/LabelChange.h"
#include "services/project/ProjectRevisionTracker.h"
#include <QObject>
#include <QSet>
#include <chrono>
#include <expected>

namespace labelqt::services {
class ProjectController;
class LabelEditController;

enum class TaskError {
    WrongThread,
    InvalidScope,
    InvalidIdentity,
    UnknownTask,
    StaleSession,
    Conflict,
    NotAllowed,
    InvalidChange,
    LimitExceeded
};

struct ProjectTaskRequest {
    QSet<QUuid> pages;
    QSet<QUuid> labels;
    QSet<QUuid> writePages;
    QSet<QUuid> writeLabels;
    QSet<ProjectField> readFields{ProjectField::Text};
    QSet<ProjectField> writeFields;
    bool allowCreate{false};
    bool readPageOrder{false};
    bool readGroups{false};
    bool readMetadata{false};
};

struct TaskLabelSnapshot {
    QUuid pageId;
    QUuid id;
    std::optional<QString> text;
    std::optional<QPointF> position;
    std::optional<QUuid> groupId;
    bool deleted{false};
};

struct TaskImageSource {
    QString name;
    QString path;
};

struct ProjectTaskSnapshot {
    QUuid taskId;
    QUuid sessionId;
    QUuid projectId;
    quint64 revision{0};
    QVector<TaskLabelSnapshot> labels;
    QVector<QUuid> pages;
    QHash<QUuid, QString> groups;
    QHash<QUuid, TaskImageSource> imageSources;
    QStringList metadata;
};

struct ProjectTaskCommitReceipt {
    QUuid taskId;
    QUuid sessionId;
    quint64 revision{0};
    LabelCommitReceipt labels;
};

// Host-thread gateway. Workers receive snapshots by value and queue submit() onto this QObject's thread.
// Scope and dependencies are retained by the host, never trusted from an echoed worker snapshot.
class ProjectTaskService final : public QObject {
public:
    ProjectTaskService(ProjectController& project, LabelEditController& editor, QObject* parent = nullptr);
    std::expected<ProjectTaskSnapshot, TaskError> capture(const ProjectTaskRequest& request);
    std::expected<ProjectTaskCommitReceipt, TaskError> submit(QUuid taskId, QVector<LabelChange> changes,
                                                              const QString& commandText);
    std::expected<void, TaskError> cancel(QUuid taskId);

private:
    struct Task {
        QUuid session;
        ProjectTaskRequest request;
        QHash<QUuid, QUuid> labels;
        QHash<ProjectResource, quint64> dependencies;
        std::chrono::steady_clock::time_point started;
        QHash<QString, FileRevision> assets;
    };
    void prune();
    ProjectController& m_project;
    LabelEditController& m_editor;
    QHash<QUuid, Task> m_tasks;
};
} // namespace labelqt::services
