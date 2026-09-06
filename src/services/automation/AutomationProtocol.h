#pragma once

#include "services/automation/ProjectTaskService.h"
#include <QJsonObject>

namespace labelqt::core {
class Project;
}

namespace labelqt::services {

enum class AutomationScope { Project, CurrentPage, SelectedLabels };

struct AutomationTaskSpec {
    AutomationScope scope{AutomationScope::SelectedLabels};
    QSet<ProjectField> readFields;
    QSet<ProjectField> writeFields;
    bool allowCreate{false};
    bool pageOrder{false};
    bool groups{false};
    bool metadata{false};
};

struct AutomationTaskResult {
    QUuid taskId;
    QUuid sessionId;
    QVector<LabelChange> changes;
    QString summary;
    QString title;
    QString text;
    bool quiet{false};
};

// Wire codec and host-side scope planning. No live mutation or UI ownership.
namespace AutomationProtocol {
constexpr int version = 2;
std::expected<AutomationTaskSpec, QString> parseTaskSpec(const QJsonObject& object);
std::expected<ProjectTaskRequest, TaskError> plan(const AutomationTaskSpec& spec, const labelqt::core::Project& project,
                                                  int currentPage, const QVector<int>& selectedLabels);
QJsonObject input(const ProjectTaskSnapshot& snapshot, const QJsonObject& parameters, QJsonObject selection = {});
std::expected<AutomationTaskResult, QString> result(const QJsonObject& object);
} // namespace AutomationProtocol
} // namespace labelqt::services
