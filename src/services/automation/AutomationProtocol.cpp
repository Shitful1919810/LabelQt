#include "services/automation/AutomationProtocol.h"

#include "core/Project.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cmath>

namespace labelqt::services::AutomationProtocol {
namespace {
QString invalid()
{
    return QCoreApplication::translate("AutomationController",
                                       "The script returned invalid or unauthorized changes. No changes were applied.");
}

bool keys(const QJsonObject& object, const QStringList& allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key()))
            return false;
    return true;
}

QUuid id(const QJsonValue& value)
{
    if (!value.isString())
        return {};
    const QUuid uuid(value.toString());
    return uuid.toString(QUuid::WithoutBraces) == value.toString() ? uuid : QUuid{};
}

QString wireId(QUuid value)
{
    return value.toString(QUuid::WithoutBraces);
}

std::expected<QSet<ProjectField>, QString> fields(const QJsonValue& value, bool write)
{
    if (!value.isArray())
        return std::unexpected(invalid());
    const QMap<QString, ProjectField> known{{"text", ProjectField::Text},
                                            {"position", ProjectField::Position},
                                            {"group", ProjectField::Group},
                                            {"deleted", ProjectField::Deleted},
                                            {"imageSource", ProjectField::ImageSource}};
    QSet<ProjectField> result;
    for (const auto& entry : value.toArray()) {
        const auto it = known.constFind(entry.toString());
        if (it == known.cend() || result.contains(it.value()) || (write && it.value() == ProjectField::ImageSource))
            return std::unexpected(invalid());
        result.insert(it.value());
    }
    return result;
}
} // namespace

std::expected<AutomationTaskSpec, QString> parseTaskSpec(const QJsonObject& object)
{
    if (!keys(object, {"scope", "read", "write", "allowCreate", "pageOrder", "groups", "metadata"}))
        return std::unexpected(invalid());
    AutomationTaskSpec spec;
    const auto scope = object.value("scope").toString();
    if (scope == "project")
        spec.scope = AutomationScope::Project;
    else if (scope == "currentPage")
        spec.scope = AutomationScope::CurrentPage;
    else if (scope == "selectedLabels")
        spec.scope = AutomationScope::SelectedLabels;
    else
        return std::unexpected(invalid());
    const auto read = fields(object.value("read"), false);
    const auto write = fields(object.value("write"), true);
    if (!read || !write)
        return std::unexpected(invalid());
    spec.readFields = *read;
    spec.writeFields = *write;
    for (const auto& key : {"allowCreate", "pageOrder", "groups", "metadata"})
        if (object.contains(key) && !object.value(key).isBool())
            return std::unexpected(invalid());
    spec.allowCreate = object.value("allowCreate").toBool();
    spec.pageOrder = object.value("pageOrder").toBool();
    spec.groups = object.value("groups").toBool();
    spec.metadata = object.value("metadata").toBool();
    if (spec.allowCreate && spec.scope == AutomationScope::SelectedLabels)
        return std::unexpected(invalid());
    return spec;
}

std::expected<ProjectTaskRequest, TaskError> plan(const AutomationTaskSpec& spec, const labelqt::core::Project& project,
                                                  int currentPage, const QVector<int>& selectedLabels)
{
    ProjectTaskRequest request;
    request.readFields = spec.readFields;
    request.writeFields = spec.writeFields;
    request.allowCreate = spec.allowCreate;
    request.readPageOrder = spec.pageOrder;
    request.readGroups = spec.groups;
    request.readMetadata = spec.metadata;
    if (spec.scope == AutomationScope::Project) {
        for (const auto& page : project.images())
            request.pages.insert(page.id);
    }
    else {
        if (currentPage < 0 || currentPage >= project.images().size())
            return std::unexpected(TaskError::InvalidScope);
        const auto& page = project.images().at(currentPage);
        if (spec.scope == AutomationScope::CurrentPage)
            request.pages.insert(page.id);
        else {
            for (int index : selectedLabels) {
                if (index < 0 || index >= page.labels.size() || page.labels.at(index).isDeleted())
                    return std::unexpected(TaskError::InvalidScope);
                request.labels.insert(page.labels.at(index).id());
            }
        }
    }
    if (request.pages.isEmpty() && request.labels.isEmpty())
        return std::unexpected(TaskError::InvalidScope);
    if (!request.writeFields.isEmpty() || request.allowCreate) {
        request.writePages = request.pages;
        request.writeLabels = request.labels;
    }
    return request;
}

QJsonObject input(const ProjectTaskSnapshot& snapshot, const QJsonObject& parameters, QJsonObject selection)
{
    QJsonArray labels;
    for (const auto& label : snapshot.labels) {
        QJsonObject value{{"id", wireId(label.id)}, {"pageId", wireId(label.pageId)}, {"deleted", label.deleted}};
        if (label.text)
            value.insert("text", *label.text);
        if (label.position)
            value.insert("position", QJsonObject{{"x", label.position->x()}, {"y", label.position->y()}});
        if (label.groupId)
            value.insert("groupId", wireId(*label.groupId));
        labels.append(value);
    }
    QJsonArray pages;
    for (const auto page : snapshot.pages) {
        QJsonObject value{{"id", wireId(page)}};
        if (snapshot.imageSources.contains(page)) {
            const auto& source = snapshot.imageSources[page];
            value.insert("imageSource", QJsonObject{{"name", source.name}, {"path", source.path}});
        }
        pages.append(value);
    }
    QJsonObject groups;
    for (auto it = snapshot.groups.cbegin(); it != snapshot.groups.cend(); ++it)
        groups.insert(wireId(it.key()), it.value());
    return {{"apiVersion", version},
            {"taskId", wireId(snapshot.taskId)},
            {"sessionId", wireId(snapshot.sessionId)},
            {"projectId", wireId(snapshot.projectId)},
            {"revision", QString::number(snapshot.revision)},
            {"parameters", parameters},
            {"selection", selection},
            {"pages", pages},
            {"labels", labels},
            {"groups", groups},
            {"metadata", QJsonArray::fromStringList(snapshot.metadata)}};
}

std::expected<AutomationTaskResult, QString> result(const QJsonObject& object)
{
    if (!keys(object, {"apiVersion", "taskId", "sessionId", "changes", "summary", "title", "text", "quiet"}) ||
        object.value("apiVersion") != QJsonValue(version) || !object.value("changes").isArray())
        return std::unexpected(invalid());
    AutomationTaskResult result;
    result.taskId = id(object.value("taskId"));
    result.sessionId = id(object.value("sessionId"));
    if (result.taskId.isNull() || result.sessionId.isNull())
        return std::unexpected(invalid());
    for (const auto& key : {"summary", "title", "text"})
        if (object.contains(key) && !object.value(key).isString())
            return std::unexpected(invalid());
    if (object.contains("quiet") && !object.value("quiet").isBool())
        return std::unexpected(invalid());
    result.summary = object.value("summary").toString();
    result.title = object.value("title").toString();
    result.text = object.value("text").toString();
    result.quiet = object.value("quiet").toBool();
    const auto changes = object.value("changes").toArray();
    if (changes.size() > 10000)
        return std::unexpected(invalid());
    QSet<QUuid> targets;
    qsizetype textSize = 0;
    for (const auto& entry : changes) {
        if (!entry.isObject())
            return std::unexpected(invalid());
        const auto value = entry.toObject();
        if (!keys(value, {"pageId", "labelId", "text", "groupId", "position", "deleted"}))
            return std::unexpected(invalid());
        LabelChange change;
        change.pageId = id(value.value("pageId"));
        if (change.pageId.isNull())
            return std::unexpected(invalid());
        if (value.contains("labelId")) {
            change.labelId = id(value.value("labelId"));
            if (change.labelId.isNull() || targets.contains(change.labelId))
                return std::unexpected(invalid());
            targets.insert(change.labelId);
        }
        if (value.contains("text")) {
            if (!value.value("text").isString())
                return std::unexpected(invalid());
            change.text = value.value("text").toString();
            textSize += change.text->size();
            if (textSize > 4 * 1024 * 1024)
                return std::unexpected(invalid());
        }
        if (value.contains("groupId")) {
            change.groupId = id(value.value("groupId"));
            if (change.groupId->isNull())
                return std::unexpected(invalid());
        }
        if (value.contains("position")) {
            const auto position = value.value("position").toObject();
            if (!keys(position, {"x", "y"}) || !position.value("x").isDouble() || !position.value("y").isDouble())
                return std::unexpected(invalid());
            const auto x = position.value("x").toDouble(), y = position.value("y").toDouble();
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
                return std::unexpected(invalid());
            change.position = QPointF(x, y);
        }
        if (value.contains("deleted")) {
            if (!value.value("deleted").isBool())
                return std::unexpected(invalid());
            change.deleted = value.value("deleted").toBool();
        }
        if ((!change.text && !change.groupId && !change.position && !change.deleted) ||
            (change.labelId.isNull() &&
             (!change.text || !change.groupId || !change.position || change.deleted.value_or(false))))
            return std::unexpected(invalid());
        result.changes.append(std::move(change));
    }
    return result;
}
} // namespace labelqt::services::AutomationProtocol
