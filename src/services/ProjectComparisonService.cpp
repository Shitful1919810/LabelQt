#include "services/ProjectComparisonService.h"

#include "services/LabelSequenceDiffService.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace labelqt::services {
namespace {
constexpr double positionEpsilon = 0.000001;

ReviewLabelSnapshot snapshotFromLabel(const QString& imageName, int labelIndex, const labelqt::core::Label& label)
{
    return {imageName, label.text(), label.group(), label.position(), labelIndex};
}

labelqt::core::Label labelFromSnapshot(const ReviewLabelSnapshot& snapshot)
{
    return labelqt::core::Label(snapshot.text, snapshot.group, snapshot.position);
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

const labelqt::core::ImageEntry* imageByName(const labelqt::core::Project& project, const QString& imageName)
{
    for (const labelqt::core::ImageEntry& image : project.images()) {
        if (image.name == imageName) {
            return &image;
        }
    }
    return nullptr;
}

QVector<ReviewLabelSnapshot> currentSnapshotsForImage(const labelqt::core::ImageEntry& image)
{
    QVector<ReviewLabelSnapshot> snapshots;
    snapshots.reserve(image.labels.size());
    for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
        const labelqt::core::Label& label = image.labels.at(labelIndex);
        if (!label.isDeleted()) {
            snapshots.append(snapshotFromLabel(image.name, labelIndex, label));
        }
    }
    return snapshots;
}

QVector<ReviewLabelSnapshot> baselineSnapshotsForImage(const ReviewMetadata& metadata, const QString& imageName)
{
    QVector<ReviewLabelSnapshot> snapshots;
    for (const ReviewLabelSnapshot& snapshot : metadata.baselineLabels()) {
        if (snapshot.imageName == imageName) {
            snapshots.append(snapshot);
        }
    }
    return snapshots;
}

QSet<QString> imageNamesInComparison(const labelqt::core::Project& currentProject, const ReviewMetadata& metadata)
{
    QSet<QString> imageNames;
    for (const labelqt::core::ImageEntry& image : currentProject.images()) {
        imageNames.insert(image.name);
    }
    for (const ReviewLabelSnapshot& snapshot : metadata.baselineLabels()) {
        if (!snapshot.imageName.isEmpty()) {
            imageNames.insert(snapshot.imageName);
        }
    }
    return imageNames;
}

ReviewChange changeFromEntry(const labelqt::core::Project& currentProject, const QString& imageName,
                             const LabelSequenceDiffEntry& entry)
{
    ReviewChange change;
    change.imageName = imageName;
    change.imageIndex = imageIndexByName(currentProject, imageName);
    change.baselineLabelIndex = entry.baselineLabelIndex;
    change.currentLabelIndex = entry.currentLabelIndex;
    change.labelIndex = change.currentLabelIndex >= 0 ? change.currentLabelIndex : change.baselineLabelIndex;

    if (entry.baselineLabelIndex < 0) {
        change.kind = ReviewChangeKind::Added;
        change.current = entry.current;
        return change;
    }
    if (entry.currentLabelIndex < 0) {
        change.kind = ReviewChangeKind::Deleted;
        change.baseline = entry.baseline;
        return change;
    }

    change.kind = ReviewChangeKind::Modified;
    change.baseline = entry.baseline;
    change.current = entry.current;
    change.textChanged = entry.baseline.text != entry.current.text;
    change.groupChanged = entry.baseline.group != entry.current.group;
    change.positionChanged = !samePosition(entry.baseline.position, entry.current.position);
    change.orderChanged = entry.moved;
    return change;
}

} // namespace

ReviewMetadata ProjectComparisonService::captureSnapshot(const labelqt::core::Project& project)
{
    ReviewMetadata metadata;
    metadata.setHasBaseline(true);
    for (const labelqt::core::ImageEntry& image : project.images()) {
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const labelqt::core::Label& label = image.labels.at(labelIndex);
            if (!label.isDeleted()) {
                metadata.setBaseline(image.name, labelIndex, snapshotFromLabel(image.name, labelIndex, label));
            }
        }
    }
    return metadata;
}

QVector<ReviewChange> ProjectComparisonService::changesForProject(const labelqt::core::Project& currentProject,
                                                                  const ReviewMetadata& baselineMetadata)
{
    QVector<ReviewChange> changes;
    if (!baselineMetadata.hasBaseline()) {
        return changes;
    }

    const QSet<QString> imageNames = imageNamesInComparison(currentProject, baselineMetadata);
    for (const QString& imageName : imageNames) {
        const QVector<ReviewLabelSnapshot> baselineLabels = baselineSnapshotsForImage(baselineMetadata, imageName);
        QVector<ReviewLabelSnapshot> currentLabels;
        if (const labelqt::core::ImageEntry* currentImage = imageByName(currentProject, imageName);
            currentImage != nullptr) {
            currentLabels = currentSnapshotsForImage(*currentImage);
        }

        const QVector<LabelSequenceDiffEntry> entries =
            LabelSequenceDiffService::diff(baselineLabels, currentLabels);
        for (const LabelSequenceDiffEntry& entry : entries) {
            ReviewChange change = changeFromEntry(currentProject, imageName, entry);
            if (change.kind == ReviewChangeKind::Modified && !isModified(change.baseline, change.current) &&
                !change.orderChanged) {
                continue;
            }
            changes.append(std::move(change));
        }
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
    const ReviewMetadata baseline = captureSnapshot(baselineProject);
    return changesForProject(currentProject, baseline);
}

QVector<labelqt::core::Label> ProjectComparisonService::baselineImageLabels(const labelqt::core::Project& currentProject,
                                                                            const ReviewMetadata& metadata,
                                                                            const QString& imageName)
{
    int maxLabelIndex = -1;
    QVector<labelqt::core::Label> currentLabels;
    if (const labelqt::core::ImageEntry* currentImage = imageByName(currentProject, imageName);
        currentImage != nullptr) {
        currentLabels = currentImage->labels;
        maxLabelIndex = std::max(maxLabelIndex, static_cast<int>(currentImage->labels.size()) - 1);
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
