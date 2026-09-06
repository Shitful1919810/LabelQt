#include "services/project/ProjectImageRenameService.h"

#include "services/io/FileRenameTransaction.h"
#include "services/project/PageSourceInfoService.h"
#include "services/proofread/ReviewMetadataService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace labelqt::services {
namespace {

QString serviceText(const char* text)
{
    return QCoreApplication::translate("ProjectImageRenameService", text);
}

[[maybe_unused]] constexpr const char* projectImageRenameServiceTranslationSources[] = {
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Pattern cannot be empty."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Unexpected '}' in pattern."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Missing '}' in pattern."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Only {index:03}-style zero padding is supported."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Unknown token {%1}."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Rendered file name cannot be empty."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Duplicate target file name."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Target file already exists."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Fix invalid file names before renaming."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "The pattern does not change any file name."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Multiple readable image files match the rendered name."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Target image does not exist."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Target image is not a file."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Target image cannot be read."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Source image does not exist."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "Source image is not a file."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "File name pattern cannot change the image directory."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "File name cannot start or end with whitespace."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "File name cannot be empty or end with a dot."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "File name contains characters that are not portable."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "File name uses a Windows reserved device name."),
    QT_TRANSLATE_NOOP("ProjectImageRenameService", "There are no image files to rename."),
};

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

std::expected<QString, QString> parsePattern(const QString& pattern, int index, const QString& stem,
                                             QVector<ProjectImageRenamePatternSpan>* spans)
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
            if (spans != nullptr) {
                spans->append({closeBeforeOpen, 1, false});
            }
            return std::unexpected(serviceText("Unexpected '}' in pattern."));
        }
        if (open < 0) {
            output += pattern.sliced(cursor);
            break;
        }

        output += pattern.sliced(cursor, open - cursor);
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0) {
            if (spans != nullptr) {
                spans->append({open, pattern.size() - open, false});
            }
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
                if (spans != nullptr) {
                    spans->append({open, close - open + 1, false});
                }
                return std::unexpected(serviceText("Only {index:03}-style zero padding is supported."));
            }
            bool validWidth = false;
            const int width = match.captured(1).toInt(&validWidth);
            if (!validWidth || width > 255) {
                if (spans != nullptr)
                    spans->append({open, close - open + 1, false});
                return std::unexpected(serviceText("Only {index:03}-style zero padding is supported."));
            }
            output += QString::number(index).rightJustified(width, QLatin1Char('0'));
        } else {
            if (spans != nullptr) {
                spans->append({open, close - open + 1, false});
            }
            return std::unexpected(serviceText("Unknown token {%1}.").arg(token));
        }
        if (spans != nullptr) {
            spans->append({open, close - open + 1, true});
        }
        cursor = close + 1;
    }

    if (output.trimmed().isEmpty()) {
        return std::unexpected(serviceText("Rendered file name cannot be empty."));
    }
    return output;
}

std::expected<QString, QString> renderPattern(const QString& pattern, int index, const QString& stem)
{
    return parsePattern(pattern, index, stem, nullptr);
}

QString pathComparisonKey(const QString& path)
{
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool samePath(const QString& lhs, const QString& rhs)
{
    return pathComparisonKey(lhs) == pathComparisonKey(rhs);
}

bool isTargetValidationError(const QString& error)
{
    return error == serviceText("Duplicate target file name.") || error == serviceText("Target file already exists.");
}

bool canReadImage(const QString& path)
{
    return QImageReader(path).canRead();
}

QString finalImagePathForConflictCheck(const ProjectImageRenameItem& item)
{
    if (item.included && item.error.isEmpty() && item.isChanged()) {
        return item.newPath;
    }
    return item.oldPath;
}

QVector<QFileInfo> readableImageStemMatches(const QDir& directory, const QString& stem)
{
    QVector<QFileInfo> matches;
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.completeBaseName() == stem && canReadImage(entry.absoluteFilePath())) {
            matches.append(entry);
        }
    }
    return matches;
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
        const int labelIndex = snapshot.labelIndex;
        newMetadata.setBaseline(newImageName, labelIndex, std::move(snapshot));
    }
    project.setCommentLines(ReviewMetadataService::rewriteCommentLines(project.commentLines(), newMetadata));
}

QString renameFailureWithRollbackDetails(QString failure, const QStringList& rollbackFailures)
{
    if (!rollbackFailures.isEmpty()) {
        failure += QStringLiteral("\n\n") + rollbackFailures.join(QLatin1Char('\n'));
    }
    return failure;
}

void validatePlanTargets(ProjectImageRenamePlan& plan)
{
    plan.error.clear();
    for (ProjectImageRenameItem& item : plan.items) {
        if (isTargetValidationError(item.error)) {
            item.error.clear();
        }
    }

    QHash<QString, int> targetPathCounts;
    for (const ProjectImageRenameItem& item : plan.items) {
        const QString finalPath = finalImagePathForConflictCheck(item);
        if (!finalPath.isEmpty()) {
            targetPathCounts[pathComparisonKey(finalPath)] += 1;
        }
    }

    for (ProjectImageRenameItem& item : plan.items) {
        if (!item.included || !item.error.isEmpty()) {
            continue;
        }
        if (targetPathCounts.value(pathComparisonKey(item.newPath)) > 1) {
            item.error = serviceText("Duplicate target file name.");
            continue;
        }
        if (item.isChanged() && !item.referenceOnly && QFileInfo::exists(item.newPath)) {
            const bool targetIsOneOfMovingSources =
                std::ranges::any_of(plan.items, [&item](const ProjectImageRenameItem& other) {
                    return other.isChanged() && !other.referenceOnly && samePath(other.oldPath, item.newPath);
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
        QString suffix = fileSuffixWithDot(oldFileInfo);
        const QString relativeDirectory = QFileInfo(item.oldName).path();
        if (options.startIndex < 0 || *orderIt > std::numeric_limits<int>::max() - options.startIndex) {
            item.error = serviceText("Fix invalid file names before renaming.");
            plan.items.append(std::move(item));
            continue;
        }
        const std::expected<QString, QString> renderedStem =
            renderPattern(options.pattern, options.startIndex + *orderIt, stem);
        if (!renderedStem) {
            item.error = renderedStem.error();
        } else {
            if (!oldFileInfo.exists()) {
                item.referenceOnly = true;
                const QDir directory(oldFileInfo.absolutePath());
                QFileInfo targetInfo(directory.absoluteFilePath(*renderedStem + suffix));
                if (!targetInfo.exists()) {
                    const QVector<QFileInfo> matches = readableImageStemMatches(directory, *renderedStem);
                    if (matches.size() == 1) {
                        targetInfo = matches.first();
                        suffix = fileSuffixWithDot(targetInfo);
                    } else if (matches.size() > 1) {
                        item.error = serviceText("Multiple readable image files match the rendered name.");
                    }
                }
                if (item.error.isEmpty() && !targetInfo.exists()) {
                    item.error = serviceText("Target image does not exist.");
                }
                if (item.error.isEmpty() && !targetInfo.isFile()) {
                    item.error = serviceText("Target image is not a file.");
                }
                if (item.error.isEmpty() && !canReadImage(targetInfo.absoluteFilePath())) {
                    item.error = serviceText("Target image cannot be read.");
                }
                if (item.error.isEmpty()) {
                    suffix = fileSuffixWithDot(targetInfo);
                }
            }
            const QString targetFileName = *renderedStem + suffix;
            item.newName = relativeDirectory.isEmpty() || relativeDirectory == QStringLiteral(".")
                               ? targetFileName
                               : QDir(relativeDirectory).filePath(targetFileName);
            item.newPath = QDir(oldFileInfo.absolutePath()).absoluteFilePath(targetFileName);
        }

        if (item.error.isEmpty() && !item.referenceOnly && !oldFileInfo.exists()) {
            item.error = serviceText("Source image does not exist.");
        }
        if (item.error.isEmpty() && !item.referenceOnly && !oldFileInfo.isFile()) {
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

    validatePlanTargets(currentPlan);
    return currentPlan;
}

ProjectImageRenamePatternAnalysis ProjectImageRenameService::analyzePattern(const QString& pattern)
{
    ProjectImageRenamePatternAnalysis analysis;
    const std::expected<QString, QString> result = parsePattern(pattern, 0, QStringLiteral("stem"), &analysis.spans);
    if (!result) {
        analysis.error = result.error();
    }
    return analysis;
}

std::expected<void, QString> ProjectImageRenameService::applyPlan(labelqt::core::Project& project,
                                                                  const ProjectImageRenamePlan& plan,
                                                                  const CommitProject& commitProject)
{
    if (!plan.isValid() || !plan.hasChanges()) {
        return std::unexpected(plan.error.isEmpty() ? serviceText("There are no image files to rename.") : plan.error);
    }

    const QHash<QString, PageSourceInfo> oldSources = PageSourceInfoService::sourcesForProject(project);
    const ReviewMetadata oldReviewMetadata = ReviewMetadataService::metadataForProject(project);

    labelqt::core::Project updated = project;
    QVector<FileRenameTransaction::Move> moves;
    QSet<int> includedIndexes;
    for (const ProjectImageRenameItem& item : plan.items) {
        if (!item.included)
            continue;
        if (item.imageIndex < 0 || item.imageIndex >= project.images().size() ||
            includedIndexes.contains(item.imageIndex) || project.images().at(item.imageIndex).path != item.oldPath) {
            return std::unexpected(serviceText("Fix invalid file names before renaming."));
        }
        includedIndexes.insert(item.imageIndex);
        updated.images()[item.imageIndex].name = item.newName;
        updated.images()[item.imageIndex].path = item.newPath;
        if (!item.isChanged() || item.referenceOnly) {
            continue;
        }
        moves.append({item.oldPath, item.newPath});
    }
    rewritePageSourceMetadata(updated, plan, oldSources);
    rewriteReviewMetadata(updated, plan, oldReviewMetadata);

    FileRenameTransaction transaction(
        std::move(moves), project.filePath().isEmpty() ? QString() : QFileInfo(project.filePath()).absolutePath());
    auto fail = [&transaction](QString error) -> std::expected<void, QString> {
        return std::unexpected(renameFailureWithRollbackDetails(std::move(error), transaction.rollback()));
    };
    try {
        const auto renamed = transaction.apply();
        if (!renamed)
            return fail(renamed.error());
        if (commitProject) {
            const auto saved = commitProject(updated);
            if (!saved)
                return fail(saved.error());
        }
    }
    catch (const std::exception& error) {
        return fail(QString::fromUtf8(error.what()));
    }
    transaction.commit();
    project = std::move(updated);
    return {};
}

} // namespace labelqt::services
