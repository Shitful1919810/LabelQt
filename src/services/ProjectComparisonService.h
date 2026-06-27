#pragma once

#include "core/Project.h"
#include "services/ReviewMetadataService.h"

#include <QVector>

namespace labelqt::services {

class ProjectComparisonService final {
public:
    static ReviewMetadata captureSnapshot(const labelqt::core::Project& project);
    static QVector<ReviewChange> changesForProject(const labelqt::core::Project& currentProject,
                                                   const ReviewMetadata& baselineMetadata);
    static QVector<ReviewChange> changesBetweenProjects(const labelqt::core::Project& baselineProject,
                                                        const labelqt::core::Project& currentProject);
    static QVector<labelqt::core::Label> baselineImageLabels(const labelqt::core::Project& currentProject,
                                                             const ReviewMetadata& metadata,
                                                             const QString& imageName);
};

} // namespace labelqt::services
