#include "services/automation/ProjectTaskService.h"

#include "core/ProjectIdentityService.h"
#include "services/label/LabelEditController.h"
#include "services/project/ProjectController.h"
#include <QThread>
#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {
bool labelField(ProjectField field)
{
    return field == ProjectField::Text || field == ProjectField::Position || field == ProjectField::Group ||
           field == ProjectField::Deleted;
}
} // namespace

ProjectTaskService::ProjectTaskService(ProjectController& project, LabelEditController& editor, QObject* parent)
    : QObject(parent), m_project(project), m_editor(editor)
{
}

void ProjectTaskService::prune()
{
    const auto session = m_project.revisions().session();
    const auto now = std::chrono::steady_clock::now();
    m_tasks.removeIf([&](const auto& entry) {
        return entry.value().session != session || now - entry.value().started > std::chrono::minutes(30);
    });
}

std::expected<ProjectTaskSnapshot, TaskError> ProjectTaskService::capture(const ProjectTaskRequest& request)
{
    if (QThread::currentThread() != thread() || !m_project.isOwnerThread())
        return std::unexpected(TaskError::WrongThread);
    if (!m_editor.belongsTo(m_project))
        return std::unexpected(TaskError::InvalidScope);
    prune();
    if (m_tasks.size() >= 64)
        return std::unexpected(TaskError::LimitExceeded);
    const auto& project = std::as_const(m_project).project();
    if (!labelqt::core::ProjectIdentityService::valid(project))
        return std::unexpected(TaskError::InvalidIdentity);
    for (auto field : request.writeFields)
        if (!labelField(field))
            return std::unexpected(TaskError::InvalidScope);
    for (auto field : request.readFields)
        if (!labelField(field) && field != ProjectField::ImageSource && field != ProjectField::GroupMembers)
            return std::unexpected(TaskError::InvalidScope);
    if (request.pages.isEmpty() && request.labels.isEmpty())
        return std::unexpected(TaskError::InvalidScope);
    if (!request.pages.contains(request.writePages) || (request.allowCreate && request.writePages.isEmpty()))
        return std::unexpected(TaskError::InvalidScope);

    const auto& revisions = m_project.revisions();
    Task task{revisions.session(), request, {}, {}, std::chrono::steady_clock::now(), {}};
    const auto depend = [&](QUuid id, ProjectField field) {
        task.dependencies.insert({id, field}, revisions.version({id, field}));
    };
    ProjectTaskSnapshot snapshot;
    snapshot.taskId = QUuid::createUuid();
    snapshot.sessionId = task.session;
    snapshot.projectId = project.id();
    snapshot.revision = revisions.revision();
    const auto fields = request.readFields | request.writeFields;
    QSet<QUuid> foundPages;
    QSet<QUuid> foundLabels;
    for (const auto& page : project.images()) {
        if (!request.pages.contains(page.id) && request.labels.isEmpty())
            continue;
        bool included = request.pages.contains(page.id);
        if (included) {
            foundPages.insert(page.id);
            depend(page.id, ProjectField::PageLabels);
        }
        for (const auto& label : page.labels) {
            if (!request.pages.contains(page.id) && !request.labels.contains(label.id()))
                continue;
            included = true;
            foundLabels.insert(label.id());
            task.labels.insert(label.id(), page.id);
            depend(label.id(), ProjectField::Exists);
            depend(label.id(), ProjectField::Deleted);
            TaskLabelSnapshot value{page.id, label.id(), {}, {}, {}, label.isDeleted()};
            for (const auto field : fields)
                if (labelField(field))
                    depend(label.id(), field);
            if (fields.contains(ProjectField::Text))
                value.text = label.text();
            if (fields.contains(ProjectField::Position))
                value.position = label.position();
            if (fields.contains(ProjectField::Group))
                value.groupId = project.groupId(label.group());
            snapshot.labels.append(std::move(value));
        }
        if (included) {
            snapshot.pages.append(page.id);
            depend(page.id, ProjectField::Exists);
            if (fields.contains(ProjectField::ImageSource)) {
                depend(page.id, ProjectField::ImageSource);
                snapshot.imageSources.insert(page.id, {page.name, page.path});
                const auto asset = FileRevision::read(page.path);
                if (!asset)
                    return std::unexpected(TaskError::InvalidScope);
                task.assets.insert(page.path, *asset);
            }
        }
    }
    if (foundPages != request.pages || !foundLabels.contains(request.labels) ||
        !foundLabels.contains(request.writeLabels))
        return std::unexpected(TaskError::InvalidScope);
    if (request.readPageOrder)
        depend(project.id(), ProjectField::PageOrder);
    if (request.readGroups || request.allowCreate || fields.contains(ProjectField::Group) ||
        fields.contains(ProjectField::GroupMembers)) {
        depend(project.id(), ProjectField::Groups);
        for (const auto& group : project.groups()) {
            const auto id = project.groupId(group);
            snapshot.groups.insert(id, group);
            depend(id, ProjectField::Exists);
            if (fields.contains(ProjectField::GroupMembers))
                depend(id, ProjectField::GroupMembers);
        }
    }
    if (request.readMetadata) {
        depend(project.id(), ProjectField::Metadata);
        snapshot.metadata = project.commentLines();
    }
    if (!request.readPageOrder)
        std::sort(snapshot.pages.begin(), snapshot.pages.end());
    if (request.pages.isEmpty())
        std::ranges::sort(snapshot.labels, {}, &TaskLabelSnapshot::id);
    else if (!request.readPageOrder)
        std::ranges::stable_sort(snapshot.labels, {}, &TaskLabelSnapshot::pageId);
    m_tasks.insert(snapshot.taskId, std::move(task));
    return snapshot;
}

std::expected<void, TaskError> ProjectTaskService::cancel(QUuid taskId)
{
    if (QThread::currentThread() != thread() || !m_project.isOwnerThread())
        return std::unexpected(TaskError::WrongThread);
    if (!m_tasks.remove(taskId))
        return std::unexpected(TaskError::UnknownTask);
    return {};
}

std::expected<ProjectTaskCommitReceipt, TaskError>
ProjectTaskService::submit(QUuid taskId, QVector<LabelChange> changes, const QString& commandText)
{
    if (QThread::currentThread() != thread() || !m_project.isOwnerThread())
        return std::unexpected(TaskError::WrongThread);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end())
        return std::unexpected(TaskError::UnknownTask);
    const Task task = it.value();
    // Consume before callbacks: a duplicate, canceled or reentrant delivery cannot apply a result twice.
    m_tasks.erase(it);
    const auto& revisions = m_project.revisions();
    if (task.session != revisions.session())
        return std::unexpected(TaskError::StaleSession);
    if (std::chrono::steady_clock::now() - task.started > std::chrono::minutes(30))
        return std::unexpected(TaskError::UnknownTask);
    for (auto dependency = task.dependencies.cbegin(); dependency != task.dependencies.cend(); ++dependency)
        if (!revisions.contains(dependency.key()) || revisions.version(dependency.key()) != dependency.value())
            return std::unexpected(TaskError::Conflict);
    for (auto asset = task.assets.cbegin(); asset != task.assets.cend(); ++asset) {
        const auto current = FileRevision::read(asset.key());
        if (!current || !current->sameContent(asset.value()))
            return std::unexpected(TaskError::Conflict);
    }
    if (changes.size() > 10000)
        return std::unexpected(TaskError::LimitExceeded);
    qsizetype textSize = 0;
    for (const auto& change : changes) {
        if (change.labelId.isNull()) {
            if (!task.request.allowCreate || !task.request.writePages.contains(change.pageId))
                return std::unexpected(TaskError::NotAllowed);
        }
        else {
            if (task.labels.value(change.labelId) != change.pageId ||
                (!task.request.writeLabels.contains(change.labelId) &&
                 !task.request.writePages.contains(change.pageId)))
                return std::unexpected(TaskError::NotAllowed);
            if ((change.text && !task.request.writeFields.contains(ProjectField::Text)) ||
                (change.position && !task.request.writeFields.contains(ProjectField::Position)) ||
                (change.groupId && !task.request.writeFields.contains(ProjectField::Group)) ||
                (change.deleted && !task.request.writeFields.contains(ProjectField::Deleted)))
                return std::unexpected(TaskError::NotAllowed);
        }
        if (change.text)
            textSize += change.text->size();
        if (textSize > 4 * 1024 * 1024)
            return std::unexpected(TaskError::LimitExceeded);
    }
    const auto committed = m_editor.commitChanges(std::move(changes), commandText);
    // Also checkpoint when the editor is used without UI callbacks (tests, future headless host).
    const auto& after = m_project.revisions();
    if (!committed)
        return std::unexpected(TaskError::InvalidChange);
    return ProjectTaskCommitReceipt{taskId, after.session(), after.revision(), *committed};
}
} // namespace labelqt::services
