#pragma once

#include "core/Project.h"
#include <QSet>

namespace labelqt::services {
struct ProjectTextQuery {
    QString text;
    bool caseSensitive{false};
    int firstPage{0};
    int lastPage{-1};
    bool allGroups{true};
    QSet<QString> groups;
};
struct ProjectTextMatch {
    int imageIndex{-1};
    int labelIndex{-1};
    qsizetype start{0};
    qsizetype length{0};
    QString imageName;
    QString text;
};
struct LabelTextReplacement {
    int imageIndex{-1};
    int labelIndex{-1};
    QString before;
    QString after;
};
struct ProjectTextSearchResult {
    QByteArray revision;
    QVector<ProjectTextMatch> matches;
};
class ProjectTextSearchService final {
public:
    static QByteArray revision(const labelqt::core::Project& project);
    static ProjectTextSearchResult search(const labelqt::core::Project& project, const ProjectTextQuery& query);
    static QVector<LabelTextReplacement> replacements(const ProjectTextSearchResult& result,
                                                      const QSet<int>& selectedMatches, const QString& replacement);
};
} // namespace labelqt::services
