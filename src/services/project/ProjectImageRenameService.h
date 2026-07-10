#pragma once

#include "core/Project.h"

#include <QString>
#include <QVector>

#include <expected>

namespace labelqt::services {

struct ProjectImageRenameOptions {
    QString pattern;
    int startIndex{1};
    QVector<int> orderedImageIndexes;
};

struct ProjectImageRenameItem {
    int imageIndex{-1};
    QString oldName;
    QString oldPath;
    QString newName;
    QString newPath;
    QString error;
    bool included{false};
    bool referenceOnly{false};

    bool isChanged() const noexcept;
    bool isValid() const noexcept;
};

struct ProjectImageRenamePlan {
    QVector<ProjectImageRenameItem> items;
    QString error;

    bool isValid() const noexcept;
    bool hasChanges() const noexcept;
};

class ProjectImageRenameService final {
public:
    static ProjectImageRenamePlan createPlan(const labelqt::core::Project& project,
                                             const ProjectImageRenameOptions& options);
    static ProjectImageRenamePlan mergePlans(ProjectImageRenamePlan currentPlan,
                                             const ProjectImageRenamePlan& incomingPlan);
    static std::expected<void, QString> applyPlan(labelqt::core::Project& project,
                                                  const ProjectImageRenamePlan& plan);

private:
    ProjectImageRenameService() = default;
};

} // namespace labelqt::services
