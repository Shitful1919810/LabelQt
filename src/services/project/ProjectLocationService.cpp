#include "services/project/ProjectLocationService.h"
#include "core/ProjectMetadataService.h"
#include "services/proofread/ReviewRoundService.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace labelqt::services {

labelqt::core::Project ProjectLocationService::forOutput(const labelqt::core::Project& project, const QString& path)
{
    auto output = project;
    output.setCommentLines(rebaseComments(project.commentLines(), project.filePath(), path));
    output.setFilePath(path);
    return output;
}

QStringList ProjectLocationService::rebaseComments(const QStringList& comments, const QString& fromPath,
                                                   const QString& toPath)
{
    if (fromPath.isEmpty() || toPath.isEmpty() ||
        QFileInfo(fromPath).absolutePath() == QFileInfo(toPath).absolutePath())
        return comments;
    auto metadata = labelqt::core::ProjectMetadataService::metadataObject(comments);
    if (metadata.contains("proofreading"))
        metadata.insert("proofreading", ReviewRoundService::rebase(metadata.value("proofreading").toObject(), fromPath, toPath));
    if (!metadata.contains("mergeSources"))
        return labelqt::core::ProjectMetadataService::rewriteCommentLines(comments, metadata);
    auto sources = metadata.value("mergeSources").toArray();
    const QDir before(QFileInfo(fromPath).absolutePath()), after(QFileInfo(toPath).absolutePath());
    for (qsizetype index = 0; index < sources.size(); ++index) {
        auto source = sources.at(index).toObject();
        const auto path = source.value("sourcePath").toString();
        if (path.isEmpty())
            continue;
        source.insert("sourcePath", after.relativeFilePath(before.absoluteFilePath(path)));
        sources[index] = source;
    }
    metadata.insert("mergeSources", sources);
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(comments, metadata);
}

} // namespace labelqt::services
