#pragma once

#include "core/Project.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace labelqt::services {

struct ProjectMergeCandidate {
    int projectIndex{-1};
    QString projectPath;
    labelqt::core::ImageEntry image;
    int labelCount{0};
};

struct ProjectMergeConflict {
    QString imageName;
    QVector<ProjectMergeCandidate> candidates;
    int selectedCandidateIndex{0};
};

struct ProjectMergePageSource {
    QString imageName;
    int projectIndex{-1};
    QString projectPath;
    int labelCount{0};
};

struct ProjectMergePlan {
    labelqt::core::Project mergedProject;
    QVector<ProjectMergeConflict> conflicts;
    QVector<ProjectMergePageSource> pageSources;
    QStringList warnings;
};

class ProjectMergeService final {
public:
    static ProjectMergePlan createPlan(const QStringList& projectPaths);
    static labelqt::core::Project mergedProjectWithSelections(ProjectMergePlan plan,
                                                                 const QVector<int>& selectedCandidateIndexes,
                                                                 const QString& outputProjectPath = {},
                                                                 const QVector<int>& imageOrder = {});

private:
    static int visibleLabelCount(const labelqt::core::ImageEntry& image);
};

} // namespace labelqt::services
