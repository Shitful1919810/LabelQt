#include "services/ReviewMetadataService.h"

#include "core/ProjectMetadataService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace labelqt::services {
namespace {
constexpr double positionEpsilon = 0.000001;

ReviewLabelSnapshot snapshotFromObject(const QJsonObject& object);
QJsonObject objectFromSnapshot(const ReviewLabelSnapshot& snapshot);

QJsonObject objectFromMetadata(const ReviewMetadata& metadata)
{
    QJsonArray labels;
    QStringList keys = metadata.baselineLabels().keys();
    keys.sort();
    for (const QString& key : std::as_const(keys)) {
        labels.append(objectFromSnapshot(metadata.baselineLabels().value(key)));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("mode"), QStringLiteral("baseline"));
    root.insert(QStringLiteral("labels"), labels);
    return root;
}

ReviewMetadata metadataFromObject(const QJsonObject& root)
{
    ReviewMetadata metadata;
    if (root.value(QStringLiteral("mode")).toString() != QStringLiteral("baseline")) {
        return metadata;
    }

    metadata.setHasBaseline(true);
    const QJsonArray labels = root.value(QStringLiteral("labels")).toArray();
    for (const QJsonValue& value : labels) {
        if (!value.isObject()) {
            continue;
        }
        const ReviewLabelSnapshot snapshot = snapshotFromObject(value.toObject());
        if (snapshot.imageName.isEmpty() || snapshot.labelIndex < 0) {
            continue;
        }
        metadata.setBaseline(snapshot.imageName, snapshot.labelIndex, snapshot);
    }
    return metadata;
}

QString stableIdKey(const QString& stableId)
{
    return QStringLiteral("id:%1").arg(stableId);
}

QString keyForSnapshot(const QString& imageName, int labelIndex, const ReviewLabelSnapshot& snapshot)
{
    return snapshot.stableId.isEmpty() ? ReviewMetadataService::keyForLabel(imageName, labelIndex)
                                       : stableIdKey(snapshot.stableId);
}

ReviewLabelSnapshot snapshotFromLabel(const QString& imageName, int labelIndex, const labelqt::core::Label& label)
{
    return {label.stableId(), imageName, label.text(), label.group(), label.position(), labelIndex};
}

ReviewLabelSnapshot snapshotFromObject(const QJsonObject& object)
{
    return {
        object.value(QStringLiteral("id")).toString(),
        object.value(QStringLiteral("page")).toString(),
        object.value(QStringLiteral("text")).toString(),
        object.value(QStringLiteral("group")).toString(),
        QPointF(object.value(QStringLiteral("x")).toDouble(), object.value(QStringLiteral("y")).toDouble()),
        object.value(QStringLiteral("labelIndex")).toInt(-1),
    };
}

QJsonObject objectFromSnapshot(const ReviewLabelSnapshot& snapshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("page"), snapshot.imageName);
    object.insert(QStringLiteral("labelIndex"), snapshot.labelIndex);
    if (!snapshot.stableId.isEmpty()) {
        object.insert(QStringLiteral("id"), snapshot.stableId);
    }
    object.insert(QStringLiteral("text"), snapshot.text);
    object.insert(QStringLiteral("group"), snapshot.group);
    object.insert(QStringLiteral("x"), snapshot.position.x());
    object.insert(QStringLiteral("y"), snapshot.position.y());
    return object;
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

labelqt::core::Label labelFromSnapshot(const ReviewLabelSnapshot& snapshot)
{
    labelqt::core::Label label(snapshot.text, snapshot.group, snapshot.position);
    if (!snapshot.stableId.isEmpty()) {
        label.setStableId(snapshot.stableId);
    }
    return label;
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
} // namespace

bool ReviewMetadata::hasBaseline() const noexcept
{
    return m_hasBaseline;
}

void ReviewMetadata::setHasBaseline(bool hasBaseline) noexcept
{
    m_hasBaseline = hasBaseline;
}

const QHash<QString, ReviewLabelSnapshot>& ReviewMetadata::baselineLabels() const noexcept
{
    return m_baselineLabels;
}

ReviewLabelSnapshot ReviewMetadata::baselineFor(const QString& imageName, int labelIndex) const
{
    const QString legacyKey = ReviewMetadataService::keyForLabel(imageName, labelIndex);
    if (m_baselineLabels.contains(legacyKey)) {
        return m_baselineLabels.value(legacyKey);
    }
    for (const ReviewLabelSnapshot& snapshot : m_baselineLabels) {
        if (snapshot.imageName == imageName && snapshot.labelIndex == labelIndex) {
            return snapshot;
        }
    }
    return {};
}

void ReviewMetadata::setBaseline(const QString& imageName, int labelIndex, ReviewLabelSnapshot snapshot)
{
    if (snapshot.imageName.isEmpty()) {
        snapshot.imageName = imageName;
    }
    if (snapshot.labelIndex < 0) {
        snapshot.labelIndex = labelIndex;
    }
    m_baselineLabels.insert(keyForSnapshot(imageName, labelIndex, snapshot), std::move(snapshot));
    m_hasBaseline = true;
}

void ReviewMetadata::clear()
{
    m_hasBaseline = false;
    m_baselineLabels.clear();
}

QString ReviewMetadataService::keyForLabel(const QString& imageName, int labelIndex)
{
    return QStringLiteral("%1#%2").arg(imageName, QString::number(labelIndex));
}

bool ReviewMetadataService::projectHasBaseline(const labelqt::core::Project& project)
{
    return metadataForProject(project).hasBaseline();
}

ReviewMetadata ReviewMetadataService::captureBaseline(const labelqt::core::Project& project)
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

ReviewMetadata ReviewMetadataService::metadataForProject(const labelqt::core::Project& project)
{
    const QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(project.commentLines());
    return metadataFromObject(metadata.value(QStringLiteral("review")).toObject());
}

QVector<ReviewChange> ReviewMetadataService::changesForProject(const labelqt::core::Project& project,
                                                               const ReviewMetadata& metadata)
{
    QVector<ReviewChange> changes;
    if (!metadata.hasBaseline()) {
        return changes;
    }

    QHash<QString, ReviewLabelSnapshot> currentSnapshots;
    QHash<QString, int> currentImageIndexes;
    for (int imageIndex = 0; imageIndex < project.images().size(); ++imageIndex) {
        const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
        for (int labelIndex = 0; labelIndex < image.labels.size(); ++labelIndex) {
            const labelqt::core::Label& label = image.labels.at(labelIndex);
            if (label.isDeleted()) {
                continue;
            }
            const ReviewLabelSnapshot snapshot = snapshotFromLabel(image.name, labelIndex, label);
            const QString key = keyForSnapshot(image.name, labelIndex, snapshot);
            currentSnapshots.insert(key, snapshot);
            currentImageIndexes.insert(key, imageIndex);
        }
    }
    const QSet<QString> orderChangedStableIds = movedStableIds(metadata.baselineLabels(), currentSnapshots);

    QStringList keys = metadata.baselineLabels().keys();
    keys.append(currentSnapshots.keys());
    keys.sort();
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    changes.reserve(keys.size());
    for (const QString& key : std::as_const(keys)) {
        const bool hadBaseline = metadata.baselineLabels().contains(key);
        const bool hasCurrent = currentSnapshots.contains(key);
        if (!hadBaseline && !hasCurrent) {
            continue;
        }

        const ReviewLabelSnapshot baseline = metadata.baselineLabels().value(key);
        const ReviewLabelSnapshot current = currentSnapshots.value(key);
        ReviewChange change;
        change.imageName = hasCurrent ? current.imageName : baseline.imageName;
        change.imageIndex = hasCurrent ? currentImageIndexes.value(key, -1) : imageIndexByName(project, change.imageName);
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

        const bool orderChanged = baseline.stableId.isEmpty() ? baseline.labelIndex != current.labelIndex
                                                              : orderChangedStableIds.contains(baseline.stableId);
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

QVector<labelqt::core::Label> ReviewMetadataService::baselineImageLabels(const labelqt::core::Project& project,
                                                                         const ReviewMetadata& metadata,
                                                                         const QString& imageName)
{
    int maxLabelIndex = -1;
    QVector<labelqt::core::Label> currentLabels;
    for (const labelqt::core::ImageEntry& image : project.images()) {
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

QStringList ReviewMetadataService::rewriteCommentLines(const QStringList& commentLines, const ReviewMetadata& metadata)
{
    QJsonObject projectMetadata = labelqt::core::ProjectMetadataService::metadataObject(commentLines);
    if (metadata.hasBaseline()) {
        projectMetadata.insert(QStringLiteral("review"), objectFromMetadata(metadata));
    } else {
        projectMetadata.remove(QStringLiteral("review"));
    }
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(commentLines, projectMetadata);
}

} // namespace labelqt::services
