#include "services/PageSourceInfoService.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {
constexpr QLatin1StringView startMarker{"# LabelQtMergeSources"};
constexpr QLatin1StringView endMarker{"# EndLabelQtMergeSources"};

QString uncommentJsonLine(QString line)
{
    line = line.trimmed();
    if (line.startsWith(QLatin1Char('#'))) {
        line.remove(0, 1);
    }
    return line.trimmed();
}

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

QString sourceCommentLine(const labelqt::core::Project& project, const PageSourceInfo& source,
                          const QString& firstImageName, const QString& lastImageName, int pageCount, int labelCount)
{
    QJsonObject object;
    object.insert(QStringLiteral("firstImage"), firstImageName);
    object.insert(QStringLiteral("lastImage"), lastImageName);
    object.insert(QStringLiteral("sourceIndex"), source.sourceIndex);
    object.insert(QStringLiteral("sourcePath"), relativeSourcePathForOutput(source.sourcePath, project.filePath()));
    object.insert(QStringLiteral("pageCount"), pageCount);
    object.insert(QStringLiteral("labelCount"), labelCount);
    return QStringLiteral("# %1").arg(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

QStringList sourceCommentLinesForCurrentOrder(const labelqt::core::Project& project,
                                              const QHash<QString, PageSourceInfo>& sourcesByImageName)
{
    QStringList lines;
    PageSourceInfo rangeSource;
    QString rangeFirstImageName;
    QString rangeLastImageName;
    int rangePageCount = 0;
    int rangeLabelCount = 0;

    auto flushRange = [&]() {
        if (rangePageCount <= 0) {
            return;
        }
        if (lines.isEmpty()) {
            lines.append(QStringLiteral("# LabelQtMergeSources v2"));
        }
        lines.append(sourceCommentLine(project, rangeSource, rangeFirstImageName, rangeLastImageName, rangePageCount,
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
    if (!lines.isEmpty()) {
        lines.append(QStringLiteral("# EndLabelQtMergeSources"));
    }
    return lines;
}

QStringList withoutSourceCommentBlock(const QStringList& commentLines)
{
    QStringList filteredLines;
    bool isInSourceBlock = false;
    for (const QString& line : commentLines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(startMarker)) {
            isInSourceBlock = true;
            continue;
        }
        if (isInSourceBlock && trimmedLine.startsWith(endMarker)) {
            isInSourceBlock = false;
            continue;
        }
        if (!isInSourceBlock) {
            filteredLines.append(line);
        }
    }
    return filteredLines;
}
} // namespace

QHash<QString, PageSourceInfo> PageSourceInfoService::sourcesForProject(const labelqt::core::Project& project)
{
    QHash<QString, PageSourceInfo> result;
    if (project.images().isEmpty() || project.commentLines().isEmpty()) {
        return result;
    }

    const QHash<QString, int> imageIndexes = imageIndexesByName(project.images());
    bool isInSourceBlock = false;
    for (const QString& line : project.commentLines()) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(startMarker)) {
            isInSourceBlock = true;
            continue;
        }
        if (trimmedLine.startsWith(endMarker)) {
            break;
        }
        if (!isInSourceBlock) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(uncommentJsonLine(line).toUtf8());
        if (!document.isObject()) {
            continue;
        }
        appendRangeSource(result, project, imageIndexes, document.object());
    }

    return result;
}

void PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(
    labelqt::core::Project& project, const QHash<QString, PageSourceInfo>& sourcesByImageName)
{
    if (sourcesByImageName.isEmpty()) {
        return;
    }

    QStringList commentLines = withoutSourceCommentBlock(project.commentLines());
    const QStringList sourceLines = sourceCommentLinesForCurrentOrder(project, sourcesByImageName);
    if (!sourceLines.isEmpty()) {
        if (!commentLines.isEmpty() && !commentLines.last().isEmpty()) {
            commentLines.append(QString());
        }
        commentLines.append(sourceLines);
    }
    project.setCommentLines(std::move(commentLines));
}

} // namespace labelqt::services
