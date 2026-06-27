#include "services/PageSourceInfoService.h"

#include "core/ProjectMetadataService.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {
QString resolvedSourcePath(const QString& sourcePath, const QString& projectPath)
{
    if (sourcePath.isEmpty() || projectPath.isEmpty() || QFileInfo(sourcePath).isAbsolute()) {
        return sourcePath;
    }

    return QDir(QFileInfo(projectPath).absolutePath()).absoluteFilePath(sourcePath);
}

QHash<QString, int> imageIndexesByName(const QVector<labelqt::core::ImageEntry>& images)
{
    QHash<QString, int> indexes;
    indexes.reserve(images.size());
    for (int i = 0; i < images.size(); ++i) {
        indexes.insert(images.at(i).name, i);
    }
    return indexes;
}

void appendRangeSource(QHash<QString, PageSourceInfo>& result, const labelqt::core::Project& project,
                       const QHash<QString, int>& imageIndexes, const QJsonObject& object)
{
    const QString firstImage =
        object.value(QStringLiteral("firstImage")).toString(object.value(QStringLiteral("image")).toString());
    if (firstImage.isEmpty() || !imageIndexes.contains(firstImage)) {
        return;
    }

    const QString lastImage = object.value(QStringLiteral("lastImage")).toString(firstImage);
    const int firstIndex = imageIndexes.value(firstImage);
    const int lastIndex = imageIndexes.value(lastImage, firstIndex);
    const int beginIndex = std::min(firstIndex, lastIndex);
    const int endIndex = std::max(firstIndex, lastIndex);

    PageSourceInfo info;
    info.sourceIndex = object.value(QStringLiteral("sourceIndex")).toInt(-1);
    info.sourcePath = resolvedSourcePath(object.value(QStringLiteral("sourcePath")).toString(), project.filePath());

    for (int i = beginIndex; i <= endIndex; ++i) {
        result.insert(project.images().at(i).name, info);
    }
}

bool sameSource(const PageSourceInfo& lhs, const PageSourceInfo& rhs)
{
    return lhs.sourceIndex == rhs.sourceIndex && lhs.sourcePath == rhs.sourcePath;
}

QString relativeSourcePathForOutput(const QString& sourcePath, const QString& projectPath)
{
    if (sourcePath.isEmpty() || projectPath.isEmpty()) {
        return sourcePath;
    }

    const QFileInfo sourceFileInfo(sourcePath);
    if (!sourceFileInfo.isAbsolute()) {
        return sourcePath;
    }

    const QString projectDirectoryPath = QFileInfo(projectPath).absolutePath();
    if (projectDirectoryPath.isEmpty()) {
        return sourcePath;
    }

    return QDir(projectDirectoryPath).relativeFilePath(sourcePath);
}

int visibleLabelCount(const labelqt::core::ImageEntry& image)
{
    return static_cast<int>(std::count_if(image.labels.cbegin(), image.labels.cend(),
                                          [](const labelqt::core::Label& label) { return !label.isDeleted(); }));
}

QJsonObject sourceObject(const labelqt::core::Project& project, const PageSourceInfo& source,
                         const QString& firstImageName, const QString& lastImageName, int pageCount, int labelCount)
{
    QJsonObject object;
    object.insert(QStringLiteral("firstImage"), firstImageName);
    object.insert(QStringLiteral("lastImage"), lastImageName);
    object.insert(QStringLiteral("sourceIndex"), source.sourceIndex);
    object.insert(QStringLiteral("sourcePath"), relativeSourcePathForOutput(source.sourcePath, project.filePath()));
    object.insert(QStringLiteral("pageCount"), pageCount);
    object.insert(QStringLiteral("labelCount"), labelCount);
    return object;
}

QJsonArray sourceArrayForCurrentOrder(const labelqt::core::Project& project,
                                      const QHash<QString, PageSourceInfo>& sourcesByImageName)
{
    QJsonArray sources;
    PageSourceInfo rangeSource;
    QString rangeFirstImageName;
    QString rangeLastImageName;
    int rangePageCount = 0;
    int rangeLabelCount = 0;

    auto flushRange = [&]() {
        if (rangePageCount <= 0) {
            return;
        }
        sources.append(sourceObject(project, rangeSource, rangeFirstImageName, rangeLastImageName, rangePageCount,
                                    rangeLabelCount));
        rangeSource = {};
        rangeFirstImageName.clear();
        rangeLastImageName.clear();
        rangePageCount = 0;
        rangeLabelCount = 0;
    };

    for (const labelqt::core::ImageEntry& image : project.images()) {
        const auto sourceIt = sourcesByImageName.constFind(image.name);
        if (sourceIt == sourcesByImageName.cend()) {
            flushRange();
            continue;
        }

        if (rangePageCount > 0 && !sameSource(rangeSource, *sourceIt)) {
            flushRange();
        }

        if (rangePageCount == 0) {
            rangeSource = *sourceIt;
            rangeFirstImageName = image.name;
        }
        rangeLastImageName = image.name;
        ++rangePageCount;
        rangeLabelCount += visibleLabelCount(image);
    }

    flushRange();
    return sources;
}
} // namespace

QHash<QString, PageSourceInfo> PageSourceInfoService::sourcesForProject(const labelqt::core::Project& project)
{
    QHash<QString, PageSourceInfo> result;
    if (project.images().isEmpty() || project.commentLines().isEmpty()) {
        return result;
    }

    const QHash<QString, int> imageIndexes = imageIndexesByName(project.images());
    const QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(project.commentLines());
    const QJsonArray sources = metadata.value(QStringLiteral("mergeSources")).toArray();
    for (const QJsonValue& value : sources) {
        if (!value.isObject()) {
            continue;
        }
        appendRangeSource(result, project, imageIndexes, value.toObject());
    }

    return result;
}

void PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(
    labelqt::core::Project& project, const QHash<QString, PageSourceInfo>& sourcesByImageName)
{
    if (sourcesByImageName.isEmpty()) {
        return;
    }

    QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(project.commentLines());
    const QJsonArray sourceArray = sourceArrayForCurrentOrder(project, sourcesByImageName);
    if (sourceArray.isEmpty()) {
        metadata.remove(QStringLiteral("mergeSources"));
    } else {
        metadata.insert(QStringLiteral("mergeSources"), sourceArray);
    }
    project.setCommentLines(labelqt::core::ProjectMetadataService::rewriteCommentLines(project.commentLines(), metadata));
}

} // namespace labelqt::services
