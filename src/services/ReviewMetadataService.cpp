#include "services/ReviewMetadataService.h"

#include "core/ProjectMetadataService.h"
#include "services/ProjectComparisonService.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {
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
    const QString key = snapshot.stableId.isEmpty() ? ReviewMetadataService::keyForLabel(imageName, labelIndex)
                                                    : QStringLiteral("id:%1").arg(snapshot.stableId);
    setBaselineWithKey(key, imageName, labelIndex, std::move(snapshot));
}

void ReviewMetadata::setBaselineWithKey(const QString& key, const QString& imageName, int labelIndex,
                                        ReviewLabelSnapshot snapshot)
{
    if (snapshot.imageName.isEmpty()) {
        snapshot.imageName = imageName;
    }
    if (snapshot.labelIndex < 0) {
        snapshot.labelIndex = labelIndex;
    }
    m_baselineLabels.insert(key, std::move(snapshot));
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
    return ProjectComparisonService::captureSnapshot(project, ProjectComparisonMatchMode::StableId);
}

ReviewMetadata ReviewMetadataService::metadataForProject(const labelqt::core::Project& project)
{
    const QJsonObject metadata = labelqt::core::ProjectMetadataService::metadataObject(project.commentLines());
    return metadataFromObject(metadata.value(QStringLiteral("review")).toObject());
}

QVector<ReviewChange> ReviewMetadataService::changesForProject(const labelqt::core::Project& project,
                                                               const ReviewMetadata& metadata)
{
    return ProjectComparisonService::changesForProject(project, metadata, ProjectComparisonMatchMode::StableId);
}

QVector<labelqt::core::Label> ReviewMetadataService::baselineImageLabels(const labelqt::core::Project& project,
                                                                         const ReviewMetadata& metadata,
                                                                         const QString& imageName)
{
    return ProjectComparisonService::baselineImageLabels(project, metadata, imageName);
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
