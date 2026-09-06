#pragma once

#include "core/Project.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <expected>
#include <functional>
#include <optional>

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
    bool operator==(const ProjectImageRenameItem&) const = default;
};

struct ProjectImageRenamePlan {
    QVector<ProjectImageRenameItem> items;
    QString error;
    std::optional<QVector<int>> finalPageOrder;

    bool isValid() const noexcept;
    bool hasChanges() const noexcept;
    bool operator==(const ProjectImageRenamePlan&) const = default;
};

struct ProjectImageRenamePatternSpan {
    qsizetype start{0};
    qsizetype length{0};
    bool valid{false};
};

struct ProjectImageRenamePatternAnalysis {
    QVector<ProjectImageRenamePatternSpan> spans;
    QString error;

    bool isValid() const noexcept { return error.isEmpty(); }
};

class ProjectImageRenameService final {
public:
    using CommitProject = std::function<std::expected<void, QString>(const labelqt::core::Project&)>;
    static ProjectImageRenamePlan createPlan(const labelqt::core::Project& project,
                                             const ProjectImageRenameOptions& options);
    static ProjectImageRenamePlan mergePlans(ProjectImageRenamePlan currentPlan,
                                             const ProjectImageRenamePlan& incomingPlan);
    static ProjectImageRenamePlan referencePlan(const labelqt::core::Project& project,
                                                const QHash<int, QString>& targets);
    static QStringList referenceCandidates(const QString& missingPath, const QString& directory);
    static ProjectImageRenamePatternAnalysis analyzePattern(const QString& pattern);
    static std::expected<void, QString> applyPlan(labelqt::core::Project& project, const ProjectImageRenamePlan& plan,
                                                  const CommitProject& commitProject = {});

private:
    ProjectImageRenameService() = default;
};

} // namespace labelqt::services
