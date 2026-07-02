#include "services/project/ProjectImageRenameService.h"

#include "services/project/PageSourceInfoService.h"
#include "services/proofread/ReviewMetadataService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <ranges>
#include <utility>

namespace labelqt::services {
namespace {

QString serviceText(const char* text)
{
    return QCoreApplication::translate("ProjectImageRenameService", text);
}

QString fileStem(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix();
    if (suffix.isEmpty()) {
        return fileInfo.fileName();
    }
    return fileInfo.completeBaseName();
}

QString fileSuffixWithDot(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix();
    return suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
}

bool containsInvalidFileNameCharacter(const QString& name)
{
    static const QRegularExpression invalidCharacters(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    return name.contains(invalidCharacters);
}

bool isWindowsReservedName(const QString& stem)
{
    static const QSet<QString> reservedNames = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9"),
    };
    return reservedNames.contains(stem.toUpper());
}

std::expected<QString, QString> renderPattern(const QString& pattern, int index, const QString& stem)
{
    if (pattern.trimmed().isEmpty()) {
        return std::unexpected(serviceText("Pattern cannot be empty."));
    }

    QString output;
    qsizetype cursor = 0;
    while (cursor < pattern.size()) {
        const qsizetype open = pattern.indexOf(QLatin1Char('{'), cursor);
        const qsizetype closeBeforeOpen = pattern.indexOf(QLatin1Char('}'), cursor);
        if (closeBeforeOpen >= 0 && (open < 0 || closeBeforeOpen < open)) {
            return std::unexpected(serviceText("Unexpected '}' in pattern."));
        }
        if (open < 0) {
            output += pattern.sliced(cursor);
            break;
        }

        output += pattern.sliced(cursor, open - cursor);
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0) {
            return std::unexpected(serviceText("Missing '}' in pattern."));
        }

        const QString token = pattern.sliced(open + 1, close - open - 1);
        if (token == QStringLiteral("stem")) {
            output += stem;
        } else if (token == QStringLiteral("index")) {
            output += QString::number(index);
        } else if (token.startsWith(QStringLiteral("index:"))) {
            const QString format = token.sliced(QStringLiteral("index:").size());
            static const QRegularExpression paddedIndexRegex(QStringLiteral(R"(^0([1-9]\d*)$)"));
            const QRegularExpressionMatch match = paddedIndexRegex.match(format);
            if (!match.hasMatch()) {
                return std::unexpected(serviceText("Only {index:03}-style zero padding is supported."));
            }
            const int width = match.captured(1).toInt();
            output += QString::number(index).rightJustified(width, QLatin1Char('0'));
        } else {
            return std::unexpected(serviceText("Unknown token {%1}.").arg(token));
        }
        cursor = close + 1;
    }

    if (output.trimmed().isEmpty()) {
        return std::unexpected(serviceText("Rendered file name cannot be empty."));
    }
    return output;
}

QString uniqueTemporaryPath(const QString& directoryPath, const QString& oldName)
{
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(directoryPath).filePath(QStringLiteral(".labelqt-rename-%1-%2.tmp").arg(uuid, oldName));
}

bool samePath(const QString& lhs, const QString& rhs)
{
    return QFileInfo(lhs).absoluteFilePath() == QFileInfo(rhs).absoluteFilePath();
}

QHash<QString, QString> renamedImageNamesByOldName(const ProjectImageRenamePlan& plan)
{
    QHash<QString, QString> names;
    for (const ProjectImageRenameItem& item : plan.items) {
        if (!item.oldName.isEmpty() && !item.newName.isEmpty()) {
            names.insert(item.oldName, item.newName);
        }
    }
    return names;
}

void rewritePageSourceMetadata(labelqt::core::Project& project, const ProjectImageRenamePlan& plan,
                               const QHash<QString, PageSourceInfo>& oldSources)
{
    if (oldSources.isEmpty()) {
        return;
    }

    const QHash<QString, QString> newNameByOldName = renamedImageNamesByOldName(plan);
    QHash<QString, PageSourceInfo> newSources;
    newSources.reserve(oldSources.size());
    for (auto it = oldSources.cbegin(); it != oldSources.cend(); ++it) {
        const QString newName = newNameByOldName.value(it.key(), it.key());
        newSources.insert(newName, it.value());
    }
    PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(project, newSources);
}

void rewriteReviewMetadata(labelqt::core::Project& project, const ProjectImageRenamePlan& plan,
                           const ReviewMetadata& oldMetadata)
{
    if (!oldMetadata.hasBaseline()) {
        return;
    }

    const QHash<QString, QString> newNameByOldName = renamedImageNamesByOldName(plan);
    ReviewMetadata newMetadata;
    for (ReviewLabelSnapshot snapshot : oldMetadata.baselineLabels()) {
        const QString newImageName = newNameByOldName.value(snapshot.imageName, snapshot.imageName);
        snapshot.imageName = newImageName;
        newMetadata.setBaseline(newImageName, snapshot.labelIndex, std::move(snapshot));
    }
    project.setCommentLines(ReviewMetadataService::rewriteCommentLines(project.commentLines(), newMetadata));
}

struct RenameStep {
    int itemIndex{-1};
    QString sourcePath;
    QString temporaryPath;
    QString targetPath;
};

void rollbackRenames(const QVector<RenameStep>& steps, const QSet<int>& movedToTemporary, const QSet<int>& movedToTarget)
{
    for (auto it = steps.crbegin(); it != steps.crend(); ++it) {
        const RenameStep& step = *it;
        if (movedToTarget.contains(step.itemIndex) && QFileInfo::exists(step.targetPath) &&
            !QFileInfo::exists(step.sourcePath)) {
            QFile::rename(step.targetPath, step.sourcePath);
        }
        if (movedToTemporary.contains(step.itemIndex) && QFileInfo::exists(step.temporaryPath) &&
            !QFileInfo::exists(step.sourcePath)) {
            QFile::rename(step.temporaryPath, step.sourcePath);
        }
    }
}

void validatePlanTargets(ProjectImageRenamePlan& plan)
{
    plan.error.clear();

    QHash<QString, int> targetPathCounts;
    for (const ProjectImageRenameItem& item : plan.items) {
        if (item.included && item.error.isEmpty() && !item.newPath.isEmpty()) {
            targetPathCounts[item.newPath] += 1;
        }
    }

    for (ProjectImageRenameItem& item : plan.items) {
        if (!item.included || !item.error.isEmpty()) {
            continue;
        }
        if (targetPathCounts.value(item.newPath) > 1) {
            item.error = serviceText("Duplicate target file name.");
            continue;
        }
        if (item.isChanged() && QFileInfo::exists(item.newPath)) {
            const bool targetIsOneOfMovingSources =
                std::ranges::any_of(plan.items, [&item](const ProjectImageRenameItem& other) {
                    return other.isChanged() && samePath(other.oldPath, item.newPath);
                });
            if (!targetIsOneOfMovingSources) {
                item.error = serviceText("Target file already exists.");
            }
        }
    }

    if (!plan.isValid()) {
        plan.error = serviceText("Fix invalid file names before renaming.");
    } else if (!plan.hasChanges()) {
        plan.error = serviceText("The pattern does not change any file name.");
    }
}

} // namespace

bool ProjectImageRenameItem::isChanged() const noexcept
{
    return included && !oldPath.isEmpty() && !newPath.isEmpty() && !samePath(oldPath, newPath);
}

bool ProjectImageRenameItem::isValid() const noexcept
{
    return error.isEmpty();
}

bool ProjectImageRenamePlan::isValid() const noexcept
{
    if (!error.isEmpty()) {
        return false;
    }
    return std::ranges::all_of(items, [](const ProjectImageRenameItem& item) { return item.isValid(); });
}

bool ProjectImageRenamePlan::hasChanges() const noexcept
{
    return std::ranges::any_of(items, [](const ProjectImageRenameItem& item) { return item.isChanged(); });
}

ProjectImageRenamePlan ProjectImageRenameService::createPlan(const labelqt::core::Project& project,
                                                             const ProjectImageRenameOptions& options)
{
    ProjectImageRenamePlan plan;
    plan.items.reserve(project.images().size());

    QVector<int> renameOrder = options.orderedImageIndexes;
    if (renameOrder.isEmpty()) {
        renameOrder.reserve(project.images().size());
        for (int imageIndex = 0; imageIndex < project.images().size(); ++imageIndex) {
            renameOrder.append(imageIndex);
        }
    }
    renameOrder.erase(std::remove_if(renameOrder.begin(), renameOrder.end(),
                                     [&project](int imageIndex) {
                                         return imageIndex < 0 || imageIndex >= project.images().size();
                                     }),
                      renameOrder.end());
    QSet<int> seenRenameIndexes;
    renameOrder.erase(std::remove_if(renameOrder.begin(), renameOrder.end(),
                                     [&seenRenameIndexes](int imageIndex) {
                                         if (seenRenameIndexes.contains(imageIndex)) {
                                             return true;
                                         }
                                         seenRenameIndexes.insert(imageIndex);
                                         return false;
                                     }),
                      renameOrder.end());

    QHash<int, int> renameOrderByImageIndex;
    renameOrderByImageIndex.reserve(renameOrder.size());
    for (int orderIndex = 0; orderIndex < renameOrder.size(); ++orderIndex) {
        renameOrderByImageIndex.insert(renameOrder.at(orderIndex), orderIndex);
    }

    for (int imageIndex = 0; imageIndex < project.images().size(); ++imageIndex) {
        const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
        ProjectImageRenameItem item;
        item.imageIndex = imageIndex;
        item.oldName = image.name.isEmpty() ? QFileInfo(image.path).fileName() : image.name;
        item.oldPath = image.path;
        item.newName = item.oldName;
        item.newPath = item.oldPath;

        const QFileInfo oldFileInfo(image.path);
        const auto orderIt = renameOrderByImageIndex.constFind(imageIndex);
        if (orderIt == renameOrderByImageIndex.cend()) {
            plan.items.append(std::move(item));
            continue;
        }

        item.included = true;
        const QString stem = fileStem(oldFileInfo);
        const QString suffix = fileSuffixWithDot(oldFileInfo);
        const QString relativeDirectory = QFileInfo(item.oldName).path();
        const std::expected<QString, QString> renderedStem =
            renderPattern(options.pattern, options.startIndex + *orderIt, stem);
        if (!renderedStem) {
            item.error = renderedStem.error();
        } else {
            const QString targetFileName = *renderedStem + suffix;
            item.newName = relativeDirectory.isEmpty() || relativeDirectory == QStringLiteral(".")
                               ? targetFileName
                               : QDir(relativeDirectory).filePath(targetFileName);
            item.newPath = QDir(oldFileInfo.absolutePath()).absoluteFilePath(targetFileName);
        }

        if (item.error.isEmpty() && !oldFileInfo.exists()) {
            item.error = serviceText("Source image does not exist.");
        }
        if (item.error.isEmpty() && !oldFileInfo.isFile()) {
            item.error = serviceText("Source image is not a file.");
        }
        if (item.error.isEmpty() && QFileInfo(item.newPath).absolutePath() != oldFileInfo.absolutePath()) {
            item.error = serviceText("File name pattern cannot change the image directory.");
        }
        const QString targetFileName = QFileInfo(item.newName).fileName();
        if (item.error.isEmpty() && targetFileName.trimmed() != targetFileName) {
            item.error = serviceText("File name cannot start or end with whitespace.");
        }
        if (item.error.isEmpty() && (targetFileName.endsWith(QLatin1Char('.')) || targetFileName.isEmpty())) {
            item.error = serviceText("File name cannot be empty or end with a dot.");
        }
        if (item.error.isEmpty() && containsInvalidFileNameCharacter(targetFileName)) {
            item.error = serviceText("File name contains characters that are not portable.");
        }
        if (item.error.isEmpty() && isWindowsReservedName(fileStem(QFileInfo(targetFileName)))) {
            item.error = serviceText("File name uses a Windows reserved device name.");
        }

        plan.items.append(std::move(item));
    }

    validatePlanTargets(plan);
    return plan;
}

ProjectImageRenamePlan ProjectImageRenameService::mergePlans(ProjectImageRenamePlan currentPlan,
                                                             const ProjectImageRenamePlan& incomingPlan)
{
    if (currentPlan.items.isEmpty()) {
        currentPlan = incomingPlan;
        validatePlanTargets(currentPlan);
        return currentPlan;
    }

    QHash<int, int> currentRowByImageIndex;
    currentRowByImageIndex.reserve(currentPlan.items.size());
    for (int row = 0; row < currentPlan.items.size(); ++row) {
        currentRowByImageIndex.insert(currentPlan.items.at(row).imageIndex, row);
    }

    for (const ProjectImageRenameItem& incomingItem : incomingPlan.items) {
        if (!incomingItem.included) {
            continue;
        }

        const auto rowIt = currentRowByImageIndex.constFind(incomingItem.imageIndex);
        if (rowIt == currentRowByImageIndex.cend()) {
            currentPlan.items.append(incomingItem);
        } else {
            currentPlan.items[*rowIt] = incomingItem;
        }
    }

    for (ProjectImageRenameItem& item : currentPlan.items) {
        if (item.included) {
            item.error.clear();
        }
    }
    validatePlanTargets(currentPlan);
    return currentPlan;
}

std::expected<void, QString> ProjectImageRenameService::applyPlan(labelqt::core::Project& project,
                                                                  const ProjectImageRenamePlan& plan)
{
    if (!plan.isValid() || !plan.hasChanges()) {
        return std::unexpected(plan.error.isEmpty() ? serviceText("There are no image files to rename.") : plan.error);
    }

    const QHash<QString, PageSourceInfo> oldSources = PageSourceInfoService::sourcesForProject(project);
    const ReviewMetadata oldReviewMetadata = ReviewMetadataService::metadataForProject(project);

    QVector<RenameStep> steps;
    steps.reserve(plan.items.size());
    for (int i = 0; i < plan.items.size(); ++i) {
        const ProjectImageRenameItem& item = plan.items.at(i);
        if (!item.included || !item.isChanged()) {
            continue;
        }

        const QFileInfo sourceInfo(item.oldPath);
        steps.append({i, item.oldPath, uniqueTemporaryPath(sourceInfo.absolutePath(), sourceInfo.fileName()), item.newPath});
    }

    QSet<int> movedToTemporary;
    QSet<int> movedToTarget;
    for (const RenameStep& step : std::as_const(steps)) {
        if (!QFile::rename(step.sourcePath, step.temporaryPath)) {
            rollbackRenames(steps, movedToTemporary, movedToTarget);
            return std::unexpected(serviceText("Failed to rename %1 to a temporary file.").arg(step.sourcePath));
        }
        movedToTemporary.insert(step.itemIndex);
    }

    for (const RenameStep& step : std::as_const(steps)) {
        if (!QFile::rename(step.temporaryPath, step.targetPath)) {
            rollbackRenames(steps, movedToTemporary, movedToTarget);
            return std::unexpected(serviceText("Failed to rename %1 to %2.").arg(step.temporaryPath, step.targetPath));
        }
        movedToTemporary.remove(step.itemIndex);
        movedToTarget.insert(step.itemIndex);
    }

    for (const ProjectImageRenameItem& item : plan.items) {
        if (!item.included || item.imageIndex < 0 || item.imageIndex >= project.images().size()) {
            continue;
        }
        labelqt::core::ImageEntry& image = project.images()[item.imageIndex];
        image.name = item.newName;
        image.path = item.newPath;
    }
    rewritePageSourceMetadata(project, plan, oldSources);
    rewriteReviewMetadata(project, plan, oldReviewMetadata);
    return {};
}

} // namespace labelqt::services
