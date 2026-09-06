#include "services/project/ProjectRevisionTracker.h"
#include <QSet>
#include <algorithm>
#include <optional>
#include <utility>

namespace labelqt::services {
void ProjectRevisionTracker::reset(const labelqt::core::Project& project)
{
    m_session = QUuid::createUuid();
    m_values.clear();
    m_versions.clear();
    m_previous.clear();
    checkpoint(project);
}

void ProjectRevisionTracker::checkpoint(const labelqt::core::Project& project)
{
    if (!m_values.isEmpty() && (m_previous.id() != project.id() || m_previous.filePath() != project.filePath())) {
        reset(project);
        return;
    }
    bool changed = false;
    bool membershipChanged = m_previous.groupIds() != project.groupIds();
    const auto stamp = [&](ProjectResource key) {
        if (!changed) {
            ++m_revision;
            changed = true;
        }
        m_versions[key] = m_revision;
    };
    const auto publish = [&](ProjectResource key, QVariant value) {
        if (m_values.contains(key) && m_values.value(key) == value)
            return;
        if (key.field == ProjectField::Group || key.field == ProjectField::Deleted ||
            key.field == ProjectField::PageLabels)
            membershipChanged = true;
        m_values.insert(key, std::move(value));
        stamp(key);
    };
    const auto remove = [&](ProjectResource key) {
        if (m_values.remove(key))
            stamp(key);
    };
    std::optional<QSet<QUuid>> survivingLabels;
    const auto removeLabel = [&](QUuid id) {
        if (!survivingLabels) {
            survivingLabels.emplace();
            for (const auto& page : project.images())
                for (const auto& label : page.labels)
                    survivingLabels->insert(label.id());
        }
        if (survivingLabels->contains(id))
            return;
        for (auto field : {ProjectField::Exists, ProjectField::Text, ProjectField::Position, ProjectField::Group,
                           ProjectField::Deleted})
            remove({id, field});
        membershipChanged = true;
    };

    QList<QUuid> pages;
    QList<QUuid> groups;
    for (const auto& group : project.groups()) {
        const auto id = project.groupId(group);
        groups.append(id);
        publish({id, ProjectField::Exists}, group);
    }
    for (const auto& id : m_previous.groupIds()) {
        if (!groups.contains(id)) {
            remove({id, ProjectField::Exists});
            remove({id, ProjectField::GroupMembers});
        }
    }
    QHash<QUuid, const labelqt::core::ImageEntry*> previousPages;
    for (const auto& page : std::as_const(m_previous).images())
        previousPages.insert(page.id, &page);
    for (const auto& page : project.images()) {
        pages.append(page.id);
        publish({page.id, ProjectField::Exists}, true);
        publish({page.id, ProjectField::ImageSource}, QStringList{page.name, page.path});
        const auto* previous = previousPages.take(page.id);
        // Qt's implicitly shared vectors let unchanged pages skip label traversal entirely.
        if (previous && previous->labels.constData() == page.labels.constData() &&
            previous->labels.size() == page.labels.size() && m_previous.groupIds() == project.groupIds())
            continue;
        QList<QUuid> labels;
        QSet<QUuid> present;
        for (const auto& label : page.labels) {
            labels.append(label.id());
            present.insert(label.id());
            publish({label.id(), ProjectField::Exists}, page.id);
            publish({label.id(), ProjectField::Text}, label.text());
            publish({label.id(), ProjectField::Position}, label.position());
            publish({label.id(), ProjectField::Group}, project.groupId(label.group()));
            publish({label.id(), ProjectField::Deleted}, label.isDeleted());
        }
        if (previous)
            for (const auto& label : previous->labels)
                if (!present.contains(label.id()))
                    removeLabel(label.id());
        publish({page.id, ProjectField::PageLabels}, QVariant::fromValue(labels));
    }
    for (const auto* page : previousPages) {
        for (const auto& label : page->labels)
            removeLabel(label.id());
        for (auto field : {ProjectField::Exists, ProjectField::ImageSource, ProjectField::PageLabels})
            remove({page->id, field});
    }
    if (membershipChanged) {
        QHash<QUuid, QList<QUuid>> members;
        for (const auto& page : project.images())
            for (const auto& label : page.labels)
                if (!label.isDeleted())
                    members[project.groupId(label.group())].append(label.id());
        for (const auto& id : groups) {
            auto list = members.value(id);
            std::sort(list.begin(), list.end());
            publish({id, ProjectField::GroupMembers}, QVariant::fromValue(list));
        }
    }
    publish({project.id(), ProjectField::PageOrder}, QVariant::fromValue(pages));
    publish({project.id(), ProjectField::Groups}, QVariant::fromValue(groups));
    publish({project.id(), ProjectField::Metadata}, project.commentLines());
    // Version tombstones are intentionally not rolled back with the implicitly shared content snapshot.
    m_previous = project;
    // Keep checkpoint storage independent of native edits' short-lived page references, while sharing label values.
    m_previous.images().detach();
}
} // namespace labelqt::services
