#include "services/proofread/ProjectComparisonService.h"

#include "services/proofread/LabelSequenceDiffService.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSize>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

namespace labelqt::services {
namespace {
constexpr double positionEpsilon = 0.000001;
constexpr int imageHashWidth = 32;
constexpr int imageHashHeight = 16;
constexpr int maximumImageHashDistance = 16;

struct PageFingerprint {
    QString imageName;
    QSize imageSize;
    QVector<quint64> hashWords;
    bool valid{false};
};

ReviewLabelSnapshot snapshotFromLabel(const QString& imageName, int labelIndex, const labelqt::core::Label& label)
{
    return {imageName, label.text(), label.group(), label.position(), labelIndex, label.id()};
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

QString normalizedPageName(const QString& imageName)
{
    const QString fileName = QFileInfo(imageName).fileName();
    return fileName.isEmpty() ? imageName : fileName;
}

int bitCount(quint64 value)
{
    return static_cast<int>(std::popcount(static_cast<std::uint64_t>(value)));
}

int imageHashDistance(const PageFingerprint& lhs, const PageFingerprint& rhs)
{
    if (lhs.hashWords.size() != rhs.hashWords.size()) {
        return maximumImageHashDistance + 1;
    }

    int distance = 0;
    for (int i = 0; i < lhs.hashWords.size(); ++i) {
        distance += bitCount(lhs.hashWords.at(i) ^ rhs.hashWords.at(i));
        if (distance > maximumImageHashDistance) {
            return distance;
        }
    }
    return distance;
}

QString fingerprintCacheKey(const labelqt::core::ImageEntry& image)
{
    const QFileInfo fileInfo(image.path);
    return QStringLiteral("%1|%2|%3")
        .arg(fileInfo.absoluteFilePath())
        .arg(fileInfo.size())
        .arg(fileInfo.lastModified().toMSecsSinceEpoch());
}

constexpr qsizetype maximumFingerprintCacheEntries = 128;

struct FingerprintCache {
    QHash<QString, PageFingerprint> entries;
    QList<QString> usageOrder;

    std::optional<PageFingerprint> find(const QString& key)
    {
        const auto it = entries.constFind(key);
        if (it == entries.cend()) return std::nullopt;
        usageOrder.removeOne(key);
        usageOrder.append(key);
        return it.value();
    }

    void insert(QString key, PageFingerprint fingerprint)
    {
        entries.insert(key, std::move(fingerprint));
        usageOrder.removeOne(key);
        usageOrder.append(key);
        while (usageOrder.size() > maximumFingerprintCacheEntries) {
            entries.remove(usageOrder.takeFirst());
        }
    }
};

FingerprintCache& fingerprintCache()
{
    static FingerprintCache cache;
    return cache;
}

QMutex& fingerprintCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

PageFingerprint fingerprintForImage(const labelqt::core::ImageEntry& image)
{
    const QString cacheKey = fingerprintCacheKey(image);
    {
        QMutexLocker locker(&fingerprintCacheMutex());
        if (const auto cached = fingerprintCache().find(cacheKey)) return *cached;
    }

    PageFingerprint fingerprint;
    fingerprint.imageName = image.name;

    QImageReader reader(image.path);
    reader.setAutoTransform(true);
    fingerprint.imageSize = reader.size();
    if (!fingerprint.imageSize.isValid()) {
        QMutexLocker locker(&fingerprintCacheMutex());
        fingerprintCache().insert(cacheKey, fingerprint);
        return fingerprint;
    }

    reader.setScaledSize(QSize(imageHashWidth, imageHashHeight));
    const QImage thumbnail = reader.read().convertToFormat(QImage::Format_Grayscale8);
    if (thumbnail.isNull()) {
        QMutexLocker locker(&fingerprintCacheMutex());
        fingerprintCache().insert(cacheKey, fingerprint);
        return fingerprint;
    }

    qsizetype total = 0;
    QVector<int> values;
    values.reserve(static_cast<qsizetype>(imageHashWidth) * imageHashHeight);
    for (int y = 0; y < thumbnail.height(); ++y) {
        const uchar* line = thumbnail.constScanLine(y);
        for (int x = 0; x < thumbnail.width(); ++x) {
            const int value = line[x];
            values.append(value);
            total += value;
        }
    }

    const double average = static_cast<double>(total) / static_cast<double>(std::max<qsizetype>(1, values.size()));
    fingerprint.hashWords.resize((values.size() + 63) / 64);
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i) < average) {
            continue;
        }
        fingerprint.hashWords[i / 64] |= quint64{1} << (i % 64);
    }
    fingerprint.valid = true;
    {
        QMutexLocker locker(&fingerprintCacheMutex());
        fingerprintCache().insert(cacheKey, fingerprint);
    }
    return fingerprint;
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

QSet<QString> baselineImageNames(const ReviewMetadata& metadata)
{
    QSet<QString> imageNames;
    for (const ReviewLabelSnapshot& snapshot : metadata.baselineLabels()) {
        if (!snapshot.imageName.isEmpty()) {
            imageNames.insert(snapshot.imageName);
        }
    }
    return imageNames;
}

QVector<ProjectComparisonPagePair> identicalPagePairs(const labelqt::core::Project& currentProject,
                                                      const ReviewMetadata& metadata)
{
    QVector<ProjectComparisonPagePair> pairs;
    const QSet<QString> baselineNames = baselineImageNames(metadata);
    QSet<QString> pairedNames;
    for (const labelqt::core::ImageEntry& image : currentProject.images()) {
        pairs.append({image.name, image.name, ProjectPageMatchKind::Name});
        pairedNames.insert(image.name);
    }
    for (const QString& baselineName : baselineNames) {
        if (!pairedNames.contains(baselineName)) {
            pairs.append({baselineName, baselineName, ProjectPageMatchKind::Unmatched});
        }
    }
    return pairs;
}

QHash<QString, QString> uniqueBaselineNamesByNormalizedName(const ReviewMetadata& metadata)
{
    QHash<QString, QString> names;
    QSet<QString> ambiguousNames;
    for (const QString& imageName : baselineImageNames(metadata)) {
        const QString key = normalizedPageName(imageName);
        if (key.isEmpty()) {
            continue;
        }
        if (names.contains(key)) {
            ambiguousNames.insert(key);
            names.remove(key);
            continue;
        }
        if (!ambiguousNames.contains(key)) {
            names.insert(key, imageName);
        }
    }
    return names;
}

QVector<ProjectComparisonPagePair> normalizedNamePagePairs(const labelqt::core::Project& baselineProject,
                                                           const labelqt::core::Project& currentProject,
                                                           const ReviewMetadata& metadata)
{
    QVector<ProjectComparisonPagePair> pairs;
    QSet<QString> pairedBaselineNames;
    QSet<QString> pairedCurrentNames;
    const QHash<QString, QString> baselineByName = uniqueBaselineNamesByNormalizedName(metadata);

    if (baselineProject.id() == currentProject.id()) {
        QHash<QUuid, const labelqt::core::ImageEntry*> pagesById;
        for (const auto& page : baselineProject.images()) pagesById.insert(page.id, &page);
        for (const auto& page : currentProject.images()) {
            if (const auto* before = pagesById.value(page.id)) {
                pairs.append({before->name, page.name, ProjectPageMatchKind::Name,
                              before->labelIdentityGeneration == page.labelIdentityGeneration});
                pairedBaselineNames.insert(before->name);
                pairedCurrentNames.insert(page.name);
            }
        }
        // In a known lineage, a missing page ID denotes an actual insertion/deletion, not a filename match.
        for (const auto& page : currentProject.images())
            if (!pairedCurrentNames.contains(page.name))
                pairs.append({{}, page.name, ProjectPageMatchKind::Unmatched});
        for (const auto& page : baselineProject.images())
            if (!pairedBaselineNames.contains(page.name))
                pairs.append({page.name, {}, ProjectPageMatchKind::Unmatched});
        return pairs;
    }

    for (const labelqt::core::ImageEntry& currentImage : currentProject.images()) {
        const QString currentName = currentImage.name;
        const QString key = normalizedPageName(currentName);
        if (!baselineByName.contains(key)) {
            continue;
        }
        const QString baselineName = baselineByName.value(key);
        pairs.append({baselineName, currentName, ProjectPageMatchKind::Name});
        pairedBaselineNames.insert(baselineName);
        pairedCurrentNames.insert(currentName);
    }

    QHash<QString, PageFingerprint> baselineFingerprints;
    for (const labelqt::core::ImageEntry& baselineImage : baselineProject.images()) {
        if (!pairedBaselineNames.contains(baselineImage.name)) {
            baselineFingerprints.insert(baselineImage.name, fingerprintForImage(baselineImage));
        }
    }

    for (const labelqt::core::ImageEntry& currentImage : currentProject.images()) {
        if (pairedCurrentNames.contains(currentImage.name)) {
            continue;
        }

        const PageFingerprint currentFingerprint = fingerprintForImage(currentImage);
        if (!currentFingerprint.valid) {
            continue;
        }

        QString bestBaselineName;
        int bestDistance = maximumImageHashDistance + 1;
        for (auto it = baselineFingerprints.cbegin(); it != baselineFingerprints.cend(); ++it) {
            const PageFingerprint& baselineFingerprint = it.value();
            if (!baselineFingerprint.valid || baselineFingerprint.imageSize != currentFingerprint.imageSize) {
                continue;
            }
            const int distance = imageHashDistance(baselineFingerprint, currentFingerprint);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestBaselineName = it.key();
            }
        }

        if (!bestBaselineName.isEmpty() && bestDistance <= maximumImageHashDistance) {
            pairs.append({bestBaselineName, currentImage.name, ProjectPageMatchKind::ImageFingerprint});
            pairedBaselineNames.insert(bestBaselineName);
            pairedCurrentNames.insert(currentImage.name);
            baselineFingerprints.remove(bestBaselineName);
        }
    }

    for (const labelqt::core::ImageEntry& currentImage : currentProject.images()) {
        if (!pairedCurrentNames.contains(currentImage.name)) {
            pairs.append({currentImage.name, currentImage.name, ProjectPageMatchKind::Unmatched});
            pairedCurrentNames.insert(currentImage.name);
        }
    }

    for (const QString& baselineName : baselineImageNames(metadata)) {
        if (!pairedBaselineNames.contains(baselineName)) {
            pairs.append({baselineName, baselineName, ProjectPageMatchKind::Unmatched});
        }
    }
    return pairs;
}

QVector<ProjectComparisonPagePair> orderPagePairs(const labelqt::core::Project& baselineProject,
                                                  const labelqt::core::Project& currentProject)
{
    QVector<ProjectComparisonPagePair> pairs;
    const qsizetype pairCount = std::min(baselineProject.images().size(), currentProject.images().size());
    pairs.reserve(std::max(baselineProject.images().size(), currentProject.images().size()));
    for (qsizetype i = 0; i < pairCount; ++i) {
        pairs.append(
            {baselineProject.images().at(i).name, currentProject.images().at(i).name, ProjectPageMatchKind::Order});
    }
    for (qsizetype i = pairCount; i < baselineProject.images().size(); ++i) {
        pairs.append({baselineProject.images().at(i).name, baselineProject.images().at(i).name,
                      ProjectPageMatchKind::Unmatched});
    }
    for (qsizetype i = pairCount; i < currentProject.images().size(); ++i) {
        pairs.append({currentProject.images().at(i).name, currentProject.images().at(i).name,
                      ProjectPageMatchKind::Unmatched});
    }
    return pairs;
}

int matchedPageCount(const QVector<ProjectComparisonPagePair>& pagePairs)
{
    int count = 0;
    for (const ProjectComparisonPagePair& pair : pagePairs) {
        if (pair.matchKind == ProjectPageMatchKind::Name ||
            pair.matchKind == ProjectPageMatchKind::ImageFingerprint ||
            pair.matchKind == ProjectPageMatchKind::Order) {
            ++count;
        }
    }
    return count;
}

ReviewChange changeFromEntry(const labelqt::core::Project& currentProject, const QString& currentImageName,
                             const QString& baselineImageName,
                             const LabelSequenceDiffEntry& entry)
{
    ReviewChange change;
    change.imageName = currentImageName;
    change.baselineImageName = baselineImageName;
    change.imageIndex = currentProject.imageIndexByName(currentImageName);
    change.baselineLabelIndex = entry.baselineLabelIndex;
    change.currentLabelIndex = entry.currentLabelIndex;
    change.labelIndex = change.currentLabelIndex >= 0 ? change.currentLabelIndex : change.baselineLabelIndex;
    change.manuallyMatched = entry.manuallyMatched;

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

QVector<ReviewChange> changesForPagePairs(const labelqt::core::Project& currentProject, const ReviewMetadata& metadata,
                                          const QVector<ProjectComparisonPagePair>& pagePairs,
                                          const QVector<ManualLabelPair>& forcedPairs = {})
{
    QVector<ReviewChange> changes;
    for (const ProjectComparisonPagePair& pagePair : pagePairs) {
        const QVector<ReviewLabelSnapshot> baselineLabels =
            baselineSnapshotsForImage(metadata, pagePair.baselineImageName);
        QVector<ReviewLabelSnapshot> currentLabels;
        if (const labelqt::core::ImageEntry* currentImage = currentProject.imageByName(pagePair.currentImageName);
            currentImage != nullptr) {
            currentLabels = currentSnapshotsForImage(*currentImage);
        }

        QVector<QPair<int, int>> pairs;
        for (const auto& forced : forcedPairs)
            if (forced.baseline.imageName == pagePair.baselineImageName &&
                forced.current.imageName == pagePair.currentImageName)
                pairs.append({forced.baseline.labelIndex, forced.current.labelIndex});
        const QVector<LabelSequenceDiffEntry> entries = pagePair.trustedLabelIdentities && pairs.isEmpty()
            ? LabelSequenceDiffService::diffByIdentity(baselineLabels, currentLabels)
            : LabelSequenceDiffService::diffWithPairs(baselineLabels, currentLabels, pairs);
        for (const LabelSequenceDiffEntry& entry : entries) {
            ReviewChange change =
                changeFromEntry(currentProject, pagePair.currentImageName, pagePair.baselineImageName, entry);
            if (change.kind == ReviewChangeKind::Modified && !isModified(change.baseline, change.current) &&
                !change.orderChanged && !change.manuallyMatched) {
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

} // namespace

std::expected<QVector<ReviewChange>, QString>
ProjectComparisonService::changesWithPairs(const labelqt::core::Project& currentProject, const ReviewMetadata& baseline,
                                           const QVector<ProjectComparisonPagePair>& pages,
                                           const QVector<ManualLabelPair>& pairs)
{
    QSet<QString> usedBefore, usedAfter;
    for (const auto& pair : pairs) {
        const auto fail = []() -> std::expected<QVector<ReviewChange>, QString> {
            return std::unexpected(QCoreApplication::translate(
                "ProjectComparisonService", "A manual pair is stale, duplicated, or outside the matched pages."));
        };
        const auto* image = currentProject.imageByName(pair.current.imageName);
        const auto before = baseline.baselineFor(pair.baseline.imageName, pair.baseline.labelIndex);
        if (!image || pair.current.labelIndex < 0 || pair.current.labelIndex >= image->labels.size() ||
            image->labels.at(pair.current.labelIndex).isDeleted() || before.labelIndex < 0)
            return fail();
        const auto after =
            snapshotFromLabel(image->name, pair.current.labelIndex, image->labels.at(pair.current.labelIndex));
        const auto beforeKey = ReviewMetadataService::keyForLabel(pair.baseline.imageName, pair.baseline.labelIndex);
        const auto afterKey = ReviewMetadataService::keyForLabel(pair.current.imageName, pair.current.labelIndex);
        if (usedBefore.contains(beforeKey) || usedAfter.contains(afterKey) ||
            ReviewSessionService::snapshotFingerprint(before) !=
                ReviewSessionService::snapshotFingerprint(pair.baseline) ||
            ReviewSessionService::snapshotFingerprint(after) != ReviewSessionService::snapshotFingerprint(pair.current))
            return fail();
        if (!std::any_of(pages.begin(), pages.end(), [&](const auto& page) {
                return page.baselineImageName == pair.baseline.imageName &&
                       page.currentImageName == pair.current.imageName;
            }))
            return fail();
        usedBefore.insert(beforeKey);
        usedAfter.insert(afterKey);
    }
    return changesForPagePairs(currentProject, baseline, pages, pairs);
}

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
    if (!baselineMetadata.hasBaseline()) {
        return {};
    }

    return changesForPagePairs(currentProject, baselineMetadata, identicalPagePairs(currentProject, baselineMetadata));
}

ProjectComparisonPlan ProjectComparisonService::planComparison(const labelqt::core::Project& baselineProject,
                                                               const labelqt::core::Project& currentProject,
                                                               ProjectPageMatchMode matchMode)
{
    ProjectComparisonPlan plan;
    plan.baselineMetadata = captureSnapshot(baselineProject);
    if (!plan.baselineMetadata.hasBaseline()) {
        return plan;
    }

    plan.canCompareByOrder = baselineProject.images().size() == currentProject.images().size() &&
                             !currentProject.images().isEmpty();
    plan.comparablePageCount = static_cast<int>(currentProject.images().size());
    plan.pagePairs = matchMode == ProjectPageMatchMode::ByOrder
        ? orderPagePairs(baselineProject, currentProject)
        : normalizedNamePagePairs(baselineProject, currentProject, plan.baselineMetadata);
    plan.matchedPageCount = matchedPageCount(plan.pagePairs);
    return plan;
}

bool ProjectComparisonService::shouldOfferOrderPageMatching(const ProjectComparisonPlan& plan)
{
    if (!plan.canCompareByOrder || plan.comparablePageCount <= 0) {
        return false;
    }

    return plan.matchedPageCount < std::max(1, plan.comparablePageCount / 2);
}

bool ProjectComparisonService::shouldOfferOrderPageMatching(const labelqt::core::Project& baselineProject,
                                                            const labelqt::core::Project& currentProject)
{
    return shouldOfferOrderPageMatching(planComparison(baselineProject, currentProject));
}

QVector<ReviewChange> ProjectComparisonService::changesBetweenProjects(const labelqt::core::Project& currentProject,
                                                                       const ProjectComparisonPlan& plan)
{
    if (!plan.baselineMetadata.hasBaseline()) {
        return {};
    }

    return changesForPagePairs(currentProject, plan.baselineMetadata, plan.pagePairs);
}

QVector<ReviewChange> ProjectComparisonService::changesBetweenProjects(const labelqt::core::Project& baselineProject,
                                                                       const labelqt::core::Project& currentProject,
                                                                       ProjectPageMatchMode matchMode)
{
    return changesBetweenProjects(currentProject, planComparison(baselineProject, currentProject, matchMode));
}

QVector<labelqt::core::Label> ProjectComparisonService::baselineImageLabels(const labelqt::core::Project& currentProject,
                                                                            const ReviewMetadata& metadata,
                                                                            const QString& imageName)
{
    return baselineImageLabels(currentProject, metadata, imageName, imageName);
}

QVector<labelqt::core::Label> ProjectComparisonService::baselineImageLabels(const labelqt::core::Project& currentProject,
                                                                            const ReviewMetadata& metadata,
                                                                            const QString& currentImageName,
                                                                            const QString& baselineImageName)
{
    int maxLabelIndex = -1;
    QVector<labelqt::core::Label> currentLabels;
    if (const labelqt::core::ImageEntry* currentImage = currentProject.imageByName(currentImageName);
        currentImage != nullptr) {
        currentLabels = currentImage->labels;
        maxLabelIndex = std::max(maxLabelIndex, static_cast<int>(currentImage->labels.size()) - 1);
    }

    QHash<int, ReviewLabelSnapshot> baselineByIndex;
    for (const ReviewLabelSnapshot& snapshot : metadata.baselineLabels()) {
        if (snapshot.imageName != baselineImageName || snapshot.labelIndex < 0) {
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
