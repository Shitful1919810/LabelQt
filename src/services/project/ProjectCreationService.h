#pragma once

#include "core/Project.h"

namespace labelqt::services {

// Builds an unsaved value; callers own discovery, validation, ordering and publication.
class ProjectCreationService final {
public:
    static QStringList defaultGroups();
    static QStringList supportedImageNameFilters();
    static void sortImageNames(QStringList& names);
    static core::Project fromImages(const QString& projectPath, const QStringList& relativeNames,
                                    const QStringList& groups, const QString& sourceName);
};

} // namespace labelqt::services
