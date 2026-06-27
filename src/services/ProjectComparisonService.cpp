#include "services/ProjectComparisonService.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace labelqt::services {
namespace {
constexpr double positionEpsilon = 0.000001;

QString stableIdKey(const QString& stableId)
{
    return QStringLiteral("id:%1").arg(stableId);
}

QString keyForSnapshot(const QString& imageName, int labelIndex, const ReviewLabelSnapshot& snapshot,
                       ProjectComparisonMatchMode matchMode)
{
    if (matchMode == ProjectComparisonMatchMode::StableId && !snapshot.stableId.isEmpty()) {
        return stableIdKey(snapshot.stableId);
    }
    return ReviewMetadataService::keyForLabel(imageName, labelIndex);
}

ReviewLabelSnapshot snapshotFromLabel(const QString& imageName, int labelIndex, const labelqt::core::Label& label)
{
    return {label.stableId(), imageName, label.text(), label.group(), label.position(), labelIndex};
}

labelqt::core::Label labelFromSnapshot(const ReviewLabelSnapshot& snapshot)
{
    labelqt::core::Label label(snapshot.text, snapshot.group, snapshot.position);
    if (!snapshot.stableId.isEmpty()) {
        label.setStableId(snapshot.stableId);
    }
    return label;
}

bool samePosition(QPointF lhs, QPointF rhs)
{
    return std::abs(lhs.x() - rhs.x()) <= positionEpsilon && std::abs(lhs.y() - rhs.y()) <= positionEpsilon;
}

bool isModified(const ReviewLabelSnapshot& baseline, const ReviewLabelSnapshot& current)
{
    return baseline.text != current.text || baseline.group != current.group ||
           !samePosition(baseline.position, current.position);
}

int imageIndexByName(const labelqt::core::Project& project, const QString& imageName)
{
    for (int i = 0; i < project.images().size(); ++i) {
        if (project.images().at(i).name == imageName) {
            return i;
        }
    }
    return -1;
}

QSet<QString> longestCommonSubsequenceIds(const QVector<QString>& baselineIds, const QVector<QString>& currentIds)
{
    QVector<QVector<int>> lengths(baselineIds.size() + 1, QVector<int>(currentIds.size() + 1));
    for (qsizetype i = 1; i <= baselineIds.size(); ++i) {
        for (qsizetype j = 1; j <= currentIds.size(); ++j) {
            if (baselineIds.at(i - 1) == currentIds.at(j - 1)) {
                lengths[i][j] = lengths.at(i - 1).at(j - 1) + 1;
            } else {
                lengths[i][j] = std::max(lengths.at(i - 1).at(j), lengths.at(i).at(j - 1));
            }
        }
    }

    QSet<QString> ids;
    qsizetype i = baselineIds.size();
    qsizetype j = currentIds.size();
    while (i > 0 && j > 0) {
        if (baselineIds.at(i - 1) == currentIds.at(j - 1)) {
            ids.insert(baselineIds.at(i - 1));
            --i;
            --j;
        } else if (lengths.at(i - 1).at(j) >= lengths.at(i).at(j - 1)) {
            --i;
        } else {
            --j;
        }
    }
    return ids;
}

QVector<QString> stableIdsByLabelIndex(QVector<ReviewLabelSnapshot> snapshots)
{
    std::sort(snapshots.begin(), snapshots.end(), [](const ReviewLabelSnapshot& lhs, const ReviewLabelSnapshot& rhs) {
        return lhs.labelIndex < rhs.labelIndex;
    });

    QVector<QString> stableIds;
    stableIds.reserve(snapshots.size());
    for (const ReviewLabelSnapshot& snapshot : std::as_const(snapshots)) {
        if (!snapshot.stableId.isEmpty()) {
            stableIds.append(snapshot.stableId);
        }
    }
    return stableIds;
}

QSet<QString> movedStableIds(const QHash<QString, ReviewLabelSnapshot>& baselineSnapshots,
                             const QHash<QString, ReviewLabelSnapshot>& currentSnapshots)
{
    QHash<QString, ReviewLabelSnapshot> currentByStableId;
    for (const ReviewLabelSnapshot& snapshot : currentSnapshots) {
        if (!snapshot.stableId.isEmpty()) {
            currentByStableId.insert(snapshot.stableId, snapshot);
        }
    }

    QHash<QString, QVector<ReviewLabelSnapshot>> baselineByImage;
    QHash<QString, QVector<ReviewLabelSnapshot>> currentByImage;
    for (const ReviewLabelSnapshot& baseline : baselineSnapshots) {
        if (baseline.stableId.isEmpty()) {
            continue;
        }
        const ReviewLabelSnapshot current = currentByStableId.value(baseline.stableId);
        if (current.stableId.isEmpty() || current.imageName != baseline.imageName) {
            continue;
        }
        baselineByImage[baseline.imageName].append(baseline);
        currentByImage[current.imageName].append(current);
    }

    QSet<QString> movedIds;
    for (auto it = baselineByImage.cbegin(); it != baselineByImage.cend(); ++it) {
        const QVector<QString> baselineIds = stableIdsByLabelIndex(it.value());
        const QVector<QString> currentIds = stableIdsByLabelIndex(currentByImage.value(it.key()));
        const QSet<QString> commonOrderIds = longestCommonSubsequenceIds(baselineIds, currentIds);
        for (const QString& stableId : baselineIds) {
            if (!commonOrderIds.contains(stableId)) {
                movedIds.insert(stableId);
            }
        }
    }
    return movedIds;
}

QHash<QString, ReviewLabelSnapshot> currentSnapshotsForProject(const labelqt::core::Project& project,
                                                               ProjectComparisonMatchMode matchMode,
                                                               QHash<QString, int>& imageIndexes)
{
    QHash<QString, ReviewLabelSnapshot> snapshots;
    for (int imageIndex = 0; imageIndex < project.images().size(); ++imageIndex) {
        const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const labelqt::core::Label& label = image.labels.at(labelIndex);
            if (label.isDeleted()) {
                continue;
            }
            const ReviewLabelSnapshot snapshot = snapshotFromLabel(image.name, labelIndex, label);
            const QString key = keyForSnapshot(image.name, labelIndex, snapshot, matchMode);
            snapshots.insert(key, snapshot);
            imageIndexes.insert(key, imageIndex);
        }
    }
    return snapshots;
}
} // namespace

ReviewMetadata ProjectComparisonService::captureSnapshot(const labelqt::core::Project& project,
                                                         ProjectComparisonMatchMode matchMode)
{
    ReviewMetadata metadata;
    metadata.setHasBaseline(true);
    for (const labelqt::core::ImageEntry& image : project.images()) {
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const labelqt::core::Label& label = image.labels.at(labelIndex);
            if (!label.isDeleted()) {
                const ReviewLabelSnapshot snapshot = snapshotFromLabel(image.name, labelIndex, label);
                const QString key = keyForSnapshot(image.name, labelIndex, snapshot, matchMode);
                metadata.setBaselineWithKey(key, image.name, labelIndex, snapshot);
            }
        }
    }
    return metadata;
}

QVector<ReviewChange> ProjectComparisonService::changesForProject(const labelqt::core::Project& currentProject,
                                                                  const ReviewMetadata& baselineMetadata,
                                                                  ProjectComparisonMatchMode matchMode)
{
    QVector<ReviewChange> changes;
    if (!baselineMetadata.hasBaseline()) {
        return changes;
    }

    QHash<QString, int> currentImageIndexes;
    const QHash<QString, ReviewLabelSnapshot> currentSnapshots =
        currentSnapshotsForProject(currentProject, matchMode, currentImageIndexes);
    const QSet<QString> orderChangedStableIds =
        matchMode == ProjectComparisonMatchMode::StableId
            ? movedStableIds(baselineMetadata.baselineLabels(), currentSnapshots)
            : QSet<QString>{};

    QStringList keys = baselineMetadata.baselineLabels().keys();
    keys.append(currentSnapshots.keys());
    keys.sort();
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    changes.reserve(keys.size());
    for (const QString& key : std::as_const(keys)) {
        const bool hadBaseline = baselineMetadata.baselineLabels().contains(key);
        const bool hasCurrent = currentSnapshots.contains(key);
        if (!hadBaseline && !hasCurrent) {
            continue;
        }

        const ReviewLabelSnapshot baseline = baselineMetadata.baselineLabels().value(key);
        const ReviewLabelSnapshot current = currentSnapshots.value(key);
        ReviewChange change;
        change.imageName = hasCurrent ? current.imageName : baseline.imageName;
        change.imageIndex =
            hasCurrent ? currentImageIndexes.value(key, -1) : imageIndexByName(currentProject, change.imageName);
        change.baselineLabelIndex = hadBaseline ? baseline.labelIndex : -1;
        change.currentLabelIndex = hasCurrent ? current.labelIndex : -1;
        change.labelIndex = change.currentLabelIndex >= 0 ? change.currentLabelIndex : change.baselineLabelIndex;

        if (!hadBaseline) {
            change.kind = ReviewChangeKind::Added;
            change.current = current;
            changes.append(std::move(change));
            continue;
        }
        if (!hasCurrent) {
            change.kind = ReviewChangeKind::Deleted;
            change.baseline = baseline;
            changes.append(std::move(change));
            continue;
        }

        const bool orderChanged = matchMode == ProjectComparisonMatchMode::StableId && !baseline.stableId.isEmpty() &&
                                  orderChangedStableIds.contains(baseline.stableId);
        if (!isModified(baseline, current) && !orderChanged) {
            continue;
        }

        change.kind = ReviewChangeKind::Modified;
        change.baseline = baseline;
        change.current = current;
        change.textChanged = baseline.text != current.text;
        change.groupChanged = baseline.group != current.group;
        change.positionChanged = !samePosition(baseline.position, current.position);
        change.orderChanged = orderChanged;
        changes.append(std::move(change));
    }

    std::sort(changes.begin(), changes.end(), [](const ReviewChange& lhs, const ReviewChange& rhs) {
        const auto lhsKey = std::tuple(lhs.imageIndex, lhs.labelIndex, static_cast<int>(lhs.kind), lhs.imageName);
        const auto rhsKey = std::tuple(rhs.imageIndex, rhs.labelIndex, static_cast<int>(rhs.kind), rhs.imageName);
        return lhsKey < rhsKey;
    });

    return changes;
}

QVector<ReviewChange> ProjectComparisonService::changesBetweenProjects(const labelqt::core::Project& baselineProject,
                                                                       const labelqt::core::Project& currentProject)
{
    const ReviewMetadata baseline = captureSnapshot(baselineProject, ProjectComparisonMatchMode::PageAndLabelIndex);
    return changesForProject(currentProject, baseline, ProjectComparisonMatchMode::PageAndLabelIndex);
}

QVector<labelqt::core::Label> ProjectComparisonService::baselineImageLabels(const labelqt::core::Project& currentProject,
                                                                            const ReviewMetadata& metadata,
                                                                            const QString& imageName)
{
    int maxLabelIndex = -1;
    QVector<labelqt::core::Label> currentLabels;
    for (const labelqt::core::ImageEntry& image : currentProject.images()) {
        if (image.name == imageName) {
            currentLabels = image.labels;
            maxLabelIndex = std::max(maxLabelIndex, static_cast<int>(image.labels.size()) - 1);
            break;
        }
    }

    QHash<int, ReviewLabelSnapshot> baselineByIndex;
    for (const ReviewLabelSnapshot& snapshot : metadata.baselineLabels()) {
        if (snapshot.imageName != imageName || snapshot.labelIndex < 0) {
            continue;
        }
        baselineByIndex.insert(snapshot.labelIndex, snapshot);
        maxLabelIndex = std::max(maxLabelIndex, snapshot.labelIndex);
    }

    QVector<labelqt::core::Label> labels;
    labels.reserve(std::max(0, maxLabelIndex + 1));
    for (int labelIndex = 0; labelIndex <= maxLabelIndex; ++labelIndex) {
        if (baselineByIndex.contains(labelIndex)) {
            labels.append(labelFromSnapshot(baselineByIndex.value(labelIndex)));
            continue;
        }

        labelqt::core::Label hiddenLabel;
        if (labelIndex >= 0 && labelIndex < currentLabels.size()) {
            hiddenLabel = currentLabels.at(labelIndex);
        }
        hiddenLabel.setDeleted(true);
        labels.append(std::move(hiddenLabel));
    }

    return labels;
}

} // namespace labelqt::services
