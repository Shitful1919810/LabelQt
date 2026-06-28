#pragma once

#include "core/Project.h"
#include "services/ReviewMetadataService.h"

#include <QVector>

namespace labelqt::services {

enum class ProjectPageMatchMode {
    ByName,
    ByOrder,
};

enum class ProjectPageMatchKind {
    Name,
    ImageFingerprint,
    Order,
    Unmatched,
};

struct ProjectComparisonPagePair {
    QString baselineImageName;
    QString currentImageName;
    ProjectPageMatchKind matchKind{ProjectPageMatchKind::Unmatched};
};

struct ProjectComparisonPlan {
    ReviewMetadata baselineMetadata;
    QVector<ProjectComparisonPagePair> pagePairs;
    int matchedPageCount{0};
    int comparablePageCount{0};
    bool canCompareByOrder{false};
};

class ProjectComparisonService final {
public:
    static ReviewMetadata captureSnapshot(const labelqt::core::Project& project);
    static QVector<ReviewChange> changesForProject(const labelqt::core::Project& currentProject,
                                                   const ReviewMetadata& baselineMetadata);
    static ProjectComparisonPlan planComparison(const labelqt::core::Project& baselineProject,
                                                const labelqt::core::Project& currentProject,
                                                ProjectPageMatchMode matchMode = ProjectPageMatchMode::ByName);
    static bool shouldOfferOrderPageMatching(const ProjectComparisonPlan& plan);
    static bool shouldOfferOrderPageMatching(const labelqt::core::Project& baselineProject,
                                             const labelqt::core::Project& currentProject);
    static QVector<ReviewChange> changesBetweenProjects(const labelqt::core::Project& currentProject,
                                                        const ProjectComparisonPlan& plan);
    static QVector<ReviewChange> changesBetweenProjects(const labelqt::core::Project& baselineProject,
                                                        const labelqt::core::Project& currentProject,
                                                        ProjectPageMatchMode matchMode = ProjectPageMatchMode::ByName);
    static QVector<labelqt::core::Label> baselineImageLabels(const labelqt::core::Project& currentProject,
                                                             const ReviewMetadata& metadata,
                                                             const QString& imageName);
    static QVector<labelqt::core::Label> baselineImageLabels(const labelqt::core::Project& currentProject,
                                                             const ReviewMetadata& metadata,
                                                             const QString& currentImageName,
                                                             const QString& baselineImageName);
};

} // namespace labelqt::services
