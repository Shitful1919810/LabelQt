#pragma once

#include "core/Project.h"
#include <QVariant>

namespace labelqt::services {

enum class ProjectField {
    Exists,
    Text,
    Position,
    Group,
    Deleted,
    PageLabels,
    PageOrder,
    Groups,
    GroupMembers,
    ImageSource,
    Metadata
};

struct ProjectResource {
    QUuid id;
    ProjectField field{ProjectField::Exists};
    bool operator==(const ProjectResource&) const = default;
};

inline size_t qHash(const ProjectResource& key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.id, static_cast<int>(key.field));
}

class ProjectRevisionTracker final {
public:
    void reset(const labelqt::core::Project& project);
    void checkpoint(const labelqt::core::Project& project);
    QUuid session() const noexcept
    {
        return m_session;
    }
    quint64 revision() const noexcept
    {
        return m_revision;
    }
    quint64 version(ProjectResource resource) const
    {
        return m_versions.value(resource);
    }
    bool contains(ProjectResource resource) const
    {
        return m_values.contains(resource);
    }

private:
    QUuid m_session{QUuid::createUuid()};
    quint64 m_revision{0};
    QHash<ProjectResource, QVariant> m_values;
    QHash<ProjectResource, quint64> m_versions;
    labelqt::core::Project m_previous;
};

} // namespace labelqt::services
