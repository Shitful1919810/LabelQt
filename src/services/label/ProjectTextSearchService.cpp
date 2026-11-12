#include "services/label/ProjectTextSearchService.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QMap>
#include <algorithm>

namespace labelqt::services {
QByteArray ProjectTextSearchService::revision(const labelqt::core::Project& project)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << project.filePath() << project.groups() << qint64(project.images().size());
    for (const auto& image : project.images()) {
        stream << image.name << image.path << qint64(image.labels.size());
        for (const auto& label : image.labels)
            stream << label.text() << label.group() << label.position() << label.isDeleted();
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
ProjectTextSearchResult ProjectTextSearchService::search(const labelqt::core::Project& project,
                                                         const ProjectTextQuery& query)
{
    ProjectTextSearchResult result{revision(project), {}};
    if (query.text.isEmpty())
        return result;
    const auto sensitivity = query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const int end = query.lastPage < 0 ? static_cast<int>(project.images().size()) - 1
                                       : std::min(query.lastPage, static_cast<int>(project.images().size()) - 1);
    for (int page = std::max(0, query.firstPage); page <= end; ++page) {
        const auto& image = project.images().at(page);
        for (int index = 0; index < image.labels.size(); ++index) {
            const auto& label = image.labels.at(index);
            if (label.isDeleted() || (!query.allGroups && !query.groups.contains(label.group())))
                continue;
            qsizetype offset = 0;
            while ((offset = label.text().indexOf(query.text, offset, sensitivity)) >= 0) {
                if (query.maxMatches >= 0 && result.matches.size() >= query.maxMatches) {
                    result.truncated = true;
                    return result;
                }
                result.matches.append({page, index, offset, query.text.size(), image.name, label.text()});
                offset += query.text.size();
            }
        }
    }
    return result;
}
QVector<LabelTextReplacement> ProjectTextSearchService::replacements(const ProjectTextSearchResult& result,
                                                                     const QSet<int>& selectedMatches,
                                                                     const QString& replacement)
{
    QMap<QPair<int, int>, QVector<ProjectTextMatch>> grouped;
    for (int index : selectedMatches) {
        if (index < 0 || index >= result.matches.size())
            continue;
        const auto& match = result.matches.at(index);
        grouped[{match.imageIndex, match.labelIndex}].append(match);
    }
    QVector<LabelTextReplacement> edits;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        auto& matches = it.value();
        std::sort(matches.begin(), matches.end(),
                  [](const auto& left, const auto& right) { return left.start > right.start; });
        QString after = matches.first().text;
        for (const auto& match : matches)
            after.replace(match.start, match.length, replacement);
        if (after != matches.first().text)
            edits.append({it.key().first, it.key().second, matches.first().text, after});
    }
    return edits;
}
} // namespace labelqt::services
