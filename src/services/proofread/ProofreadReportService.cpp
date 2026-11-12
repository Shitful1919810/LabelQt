#include "services/proofread/ProofreadReportService.h"
#include "services/proofread/ProofreadReportContent.h"

#include "services/proofread/ReviewChangeClassifier.h"
#include "services/proofread/TextDiffHtmlRenderer.h"

#include "core/AtomicFileWriter.h"
#include "services/io/ImagePageCache.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QImage>
#include <QSet>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace report {

QString escaped(QString text)
{
    return TextDiffHtmlRenderer::escapedText(std::move(text));
}

QString labelNumber(int labelIndex)
{
    return labelIndex < 0 ? QStringLiteral("-") : QString::number(labelIndex + 1);
}

QString changeKindText(const ReviewChange& change, const ProofreadReportTexts& texts)
{
    switch (change.kind) {
    case ReviewChangeKind::Comment:
        return texts.comment;
    case ReviewChangeKind::Added:
        return texts.added;
    case ReviewChangeKind::Deleted:
        return texts.deleted;
    case ReviewChangeKind::Modified:
        return texts.modified;
    }
    return texts.modified;
}

QString changeSummary(const ReviewChange& change, const ProofreadReportTexts& texts)
{
    if (change.kind != ReviewChangeKind::Modified) {
        return changeKindText(change, texts);
    }

    QStringList parts;
    for (const ReviewChangeFacet facet : ReviewChangeClassifier::facets(change)) {
        switch (facet) {
        case ReviewChangeFacet::Comment:
            parts.append(texts.comment);
            break;
        case ReviewChangeFacet::Text:
            parts.append(texts.text);
            break;
        case ReviewChangeFacet::Format:
            parts.append(texts.format);
            break;
        case ReviewChangeFacet::Group:
            parts.append(texts.group);
            break;
        case ReviewChangeFacet::Marker:
            parts.append(texts.marker);
            break;
        case ReviewChangeFacet::Order:
            parts.append(texts.order);
            break;
        }
    }
    return parts.isEmpty() ? texts.modified : parts.join(QStringLiteral(", "));
}

QString textDiffHtml(const ReviewChange& change, const ProofreadReportTexts& texts)
{
    if (change.textChanged || change.kind != ReviewChangeKind::Modified) {
        const QString beforeText = change.kind == ReviewChangeKind::Added ? QString() : change.baseline.text;
        const QString afterText = change.kind == ReviewChangeKind::Deleted ? QString() : change.current.text;
        return TextDiffHtmlRenderer::renderInlineDiff(beforeText, afterText, texts.noTextChange,
                                                      change.kind == ReviewChangeKind::Comment);
    }

    return escaped(texts.noTextChange);
}

QImage loadImage(const QString& imagePath, const ProofreadReportOptions& options)
{
    if (imagePath.isEmpty())
        return {};
    const int extent = std::clamp(options.maxImageWidth, 1, 2048);
    return ImagePageCache::previewLoader(32 * 1024 * 1024)(imagePath, QSize(extent, extent)).image;
}

QString compressedImageDataUrl(const QImage& image, const ProofreadReportOptions& options)
{
    if (image.isNull()) {
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPG", std::clamp(options.jpegQuality, 1, 100))) {
        return {};
    }

    return QStringLiteral("data:image/jpeg;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
}

struct ReportImage {
    QString source;
    QSize size;
};

ReportImage reportImageForEntry(const labelqt::core::ImageEntry* image, const ProofreadReportOptions& options)
{
    if (image == nullptr) {
        return {};
    }
    const auto preview = loadImage(image->path, options);
    return {.source = compressedImageDataUrl(preview, options), .size = preview.size()};
}

QString cssString(QString text)
{
    return text.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'"));
}

QString imageAspectRatio(const ReportImage& image)
{
    if (!image.size.isValid() || image.size.height() <= 0) {
        return QStringLiteral("4 / 3");
    }
    return QStringLiteral("%1 / %2").arg(image.size.width()).arg(image.size.height());
}

QString markerClass(ReviewChangeKind kind)
{
    switch (kind) {
    case ReviewChangeKind::Comment:
        return QStringLiteral("modified");
    case ReviewChangeKind::Added:
        return QStringLiteral("marker-added");
    case ReviewChangeKind::Deleted:
        return QStringLiteral("marker-deleted");
    case ReviewChangeKind::Modified:
        return QStringLiteral("marker-modified");
    }
    return QStringLiteral("marker-modified");
}

QPointF clampedPosition(QPointF position)
{
    return QPointF(std::clamp(position.x(), 0.0, 1.0), std::clamp(position.y(), 0.0, 1.0));
}

int beforeLabelIndex(const ReviewChange& change)
{
    return change.baselineLabelIndex >= 0 ? change.baselineLabelIndex : change.labelIndex;
}

int afterLabelIndex(const ReviewChange& change)
{
    return change.currentLabelIndex >= 0 ? change.currentLabelIndex : change.labelIndex;
}

QString reportStyle()
{
    return QStringLiteral(R"(
body { font-family: sans-serif; line-height: 1.35; margin: 24px; color: #1f2937; background: #ffffff; }
h1 { font-size: 28px; margin-bottom: 4px; }
.meta { color: #6b7280; margin-bottom: 14px; }
.page-section { break-inside: avoid; margin-top: 26px; }
.page-title { align-items: baseline; border-bottom: 2px solid #e5e7eb; display: flex; gap: 12px; margin-bottom: 10px; padding-bottom: 4px; }
.page-title h2 { font-size: 22px; margin: 0; }
.page-title .count { color: #6b7280; }
.image-pair { display: grid; gap: 12px; grid-template-columns: repeat(2, minmax(0, 1fr)); margin-bottom: 10px; }
.image-frame { background: #f9fafb; border: 1px solid #d1d5db; min-width: 0; padding: 8px; }
.image-frame h3 { color: #374151; font-size: 15px; margin: 0 0 6px; }
.image-overlay { background-position: center; background-repeat: no-repeat; background-size: contain; line-height: 0;
                 position: relative; width: 100%; }
.image-before { aspect-ratio: var(--before-ratio); background-image: var(--before-image); }
.image-after { aspect-ratio: var(--after-ratio); background-image: var(--after-image); }
.marker { border: 2px solid #fff; border-radius: 999px; box-shadow: 0 1px 4px rgba(0,0,0,.45);
          color: #fff; font-size: 13px; font-weight: 700; line-height: 1; min-width: 22px; padding: 4px 6px;
          position: absolute; text-align: center; transform: translate(-50%, -50%); }
.marker-added { background: #15803d; }
.marker-deleted { background: #b91c1c; }
.marker-modified { background: #ca8a04; }
table { border-collapse: collapse; width: 100%; table-layout: fixed; }
th, td { border: 1px solid #d1d5db; padding: 6px 8px; vertical-align: top; overflow-wrap: anywhere; }
th { background: #f3f4f6; color: #374151; text-align: left; }
tr:nth-child(even) { background: #fafafa; }
.column-label { width: 72px; }
.column-type { width: 90px; }
.column-summary { width: 120px; }
p { margin: 0; }
)");
}

QVector<QVector<int>> groupedChanges(const QVector<ReviewChange>& changes)
{
    using PageKey = QPair<QString, QPair<QString, QString>>;
    QHash<PageKey, qsizetype> groups;
    QVector<QVector<int>> rows;
    for (int i = 0; i < changes.size(); ++i) {
        const auto& change = changes.at(i);
        const PageKey key{change.pageName(), {change.baselineImageName, change.imageName}};
        if (!groups.contains(key)) {
            groups.insert(key, rows.size());
            rows.append(QVector<int>{});
        }
        rows[groups.value(key)].append(i);
    }
    return rows;
}

QPair<const core::ImageEntry*, const core::ImageEntry*> imagesForChanges(
    const QVector<int>& changeIndexes, const QVector<ReviewChange>& changes,
    const core::Project& beforeProject, const core::Project& currentProject)
{
    const labelqt::core::ImageEntry* beforeImage = nullptr;
    const labelqt::core::ImageEntry* afterImage = nullptr;
    for (const int changeIndex : changeIndexes) {
        const ReviewChange& change = changes.at(changeIndex);
        if (beforeImage == nullptr) {
            beforeImage = beforeProject.imageByName(change.baselineImageName);
        }
        if (afterImage == nullptr) {
            afterImage = currentProject.imageByName(change.imageName);
        }
        if (beforeImage != nullptr && afterImage != nullptr) {
            break;
        }
    }
    if (beforeImage == nullptr) {
        beforeImage = afterImage;
    }
    if (afterImage == nullptr) {
        afterImage = beforeImage;
    }
    return {beforeImage, afterImage};
}

QVector<Marker> markersForChanges(const QVector<int>& indexes, const QVector<ReviewChange>& changes, bool before)
{
    QVector<Marker> markers;
    for (const int index : indexes) {
        const auto& change = changes.at(index);
        if ((before && change.kind == ReviewChangeKind::Added) ||
            (!before && change.kind == ReviewChangeKind::Deleted) ||
            (change.kind == ReviewChangeKind::Comment &&
             (before ? change.baselineLabelIndex : change.currentLabelIndex) < 0))
            continue;
        markers.append({clampedPosition(before ? change.baseline.position : change.current.position),
                        labelNumber(before ? beforeLabelIndex(change) : afterLabelIndex(change)),
                        change.kind});
    }
    return markers;
}

QString detailsHtml(const ReviewChange& change, const ProofreadReportTexts& texts,
                    const ProofreadReportOptions& options)
{
    QString details = textDiffHtml(change, texts);
    if (change.manuallyMatched)
        details += QStringLiteral("<p>%1</p>").arg(escaped(texts.manualPair));
    if (options.includeReview) {
        const auto annotation = options.annotations.value(ReviewSessionService::fingerprint(change));
        const QString color = annotation.status == ReviewStatus::Reviewed ? QStringLiteral("#299c68")
                              : annotation.status == ReviewStatus::NeedsConfirmation ? QStringLiteral("#d69a19")
                                                                                     : QStringLiteral("#db505c");
        details += QStringLiteral("<p><strong><span style=\"color:%1\">%2: %3</span></strong></p>")
            .arg(color, escaped(texts.review), escaped(texts.reviewStates.value(static_cast<int>(annotation.status))));
        if (!annotation.note.isEmpty())
            details += QStringLiteral("<div style=\"white-space:pre-wrap\">%1</div>").arg(escaped(annotation.note));
    }
    if (!change.noteText.isEmpty())
        details += QStringLiteral("<div style=\"white-space:pre-wrap\"><strong>%1</strong><br>%2</div>")
            .arg(escaped(texts.comment), escaped(change.noteText));
    return details;
}

QString pageSectionHtml(const QString& pageName, const QVector<int>& changeIndexes,
                        const QVector<ReviewChange>& changes, const core::Project& beforeProject,
                        const core::Project& currentProject, const ProofreadReportTexts& texts,
                        const ProofreadReportOptions& options)
{
    const auto [beforeImage, afterImage] = imagesForChanges(changeIndexes, changes, beforeProject, currentProject);

    const bool sameImage = beforeImage != nullptr && afterImage != nullptr && beforeImage->path == afterImage->path;
    const ReportImage beforeReportImage = reportImageForEntry(beforeImage, options);
    const ReportImage afterReportImage = sameImage ? beforeReportImage : reportImageForEntry(afterImage, options);

    QString imageStyle;
    if (!beforeReportImage.source.isEmpty()) {
        imageStyle += QStringLiteral("--before-image:url('%1');--before-ratio:%2;")
                          .arg(cssString(beforeReportImage.source), imageAspectRatio(beforeReportImage));
    }
    if (!afterReportImage.source.isEmpty()) {
        imageStyle += sameImage ? QStringLiteral("--after-image:var(--before-image);--after-ratio:var(--before-ratio);")
                                : QStringLiteral("--after-image:url('%1');--after-ratio:%2;")
                                      .arg(cssString(afterReportImage.source), imageAspectRatio(afterReportImage));
    }

    QString html = QStringLiteral("<section class=\"page-section\" style=\"%1\"><div class=\"page-title\"><h2>%2</h2>"
                                  "<span class=\"count\">%3</span></div>")
                       .arg(escaped(imageStyle), escaped(pageName))
                       .arg(changeIndexes.size());

    if (!beforeReportImage.source.isEmpty() || !afterReportImage.source.isEmpty()) {
        html += QStringLiteral("<div class=\"image-pair\"><div class=\"image-frame\"><h3>%1</h3>"
                               "<div class=\"image-overlay image-before\">")
                    .arg(escaped(texts.before));
        for (const auto& marker : markersForChanges(changeIndexes, changes, true)) {
            html += QStringLiteral("<span class=\"marker %1\" style=\"left:%2%;top:%3%\">%4</span>")
                        .arg(markerClass(marker.kind == ReviewChangeKind::Deleted ? ReviewChangeKind::Deleted
                                                                                  : ReviewChangeKind::Modified))
                        .arg(marker.position.x() * 100.0, 0, 'f', 2)
                        .arg(marker.position.y() * 100.0, 0, 'f', 2)
                        .arg(escaped(marker.number));
        }
        html += QStringLiteral("</div></div><div class=\"image-frame\"><h3>%1</h3>"
                               "<div class=\"image-overlay image-after\">")
                    .arg(escaped(texts.after));
        for (const auto& marker : markersForChanges(changeIndexes, changes, false)) {
            html += QStringLiteral("<span class=\"marker %1\" style=\"left:%2%;top:%3%\">%4</span>")
                        .arg(markerClass(marker.kind == ReviewChangeKind::Added ? ReviewChangeKind::Added
                                                                                : ReviewChangeKind::Modified))
                        .arg(marker.position.x() * 100.0, 0, 'f', 2)
                        .arg(marker.position.y() * 100.0, 0, 'f', 2)
                        .arg(escaped(marker.number));
        }
        html += QStringLiteral("</div></div></div>");
    }

    html += QStringLiteral("<table><thead><tr>"
                           "<th class=\"column-label\">%1</th>"
                           "<th class=\"column-type\">%2</th>"
                           "<th class=\"column-summary\">%3</th>"
                           "<th>%4</th>"
                           "</tr></thead><tbody>")
                .arg(escaped(texts.label), escaped(texts.changeType), escaped(texts.summary),
                     escaped(texts.textDifference));
    for (const int changeIndex : changeIndexes) {
        const ReviewChange& change = changes.at(changeIndex);
        const QString details = detailsHtml(change, texts, options);
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                    .arg(escaped(labelNumber(change.labelIndex)), escaped(changeKindText(change, texts)),
                         escaped(changeSummary(change, texts)), details);
    }
    html += QStringLiteral("</tbody></table></section>");
    return html;
}

} // namespace

using namespace report;

ProofreadReportTexts ProofreadReportService::defaultTexts(const QString& before, const QString& after)
{
    return {
        .title = QCoreApplication::translate("ProofreadChangesDialog", "Proofreading Report"),
        .generatedAt = QCoreApplication::translate("ProofreadChangesDialog", "Generated at"),
        .totalChanges = QCoreApplication::translate("ProofreadChangesDialog", "Total changes"),
        .page = QCoreApplication::translate("ProofreadChangesDialog", "Page"),
        .label = QCoreApplication::translate("ProofreadChangesDialog", "Label"),
        .changeType = QCoreApplication::translate("ProofreadChangesDialog", "Change Type"),
        .summary = QCoreApplication::translate("ProofreadChangesDialog", "Summary"),
        .textDifference = QCoreApplication::translate("ProofreadChangesDialog", "Text Difference"),
        .groupChange = QCoreApplication::translate("ProofreadChangesDialog", "Group Change"),
        .markerChange = QCoreApplication::translate("ProofreadChangesDialog", "Marker Change"),
        .orderChange = QCoreApplication::translate("ProofreadChangesDialog", "Order Change"),
        .before = before,
        .after = after,
        .added = QCoreApplication::translate("ProofreadChangesDialog", "Added"),
        .deleted = QCoreApplication::translate("ProofreadChangesDialog", "Deleted"),
        .modified = QCoreApplication::translate("ProofreadChangesDialog", "Modified"),
        .text = QCoreApplication::translate("ProofreadChangesDialog", "text"),
        .format = QCoreApplication::translate("ProofreadChangesDialog", "format"),
        .group = QCoreApplication::translate("ProofreadChangesDialog", "group"),
        .marker = QCoreApplication::translate("ProofreadChangesDialog", "marker"),
        .order = QCoreApplication::translate("ProofreadChangesDialog", "order"),
        .noTextChange = QCoreApplication::translate("ProofreadChangesDialog", "No text change."),
        .filter = QCoreApplication::translate("ProofreadChangesDialog", "Filters"),
        .review = QCoreApplication::translate("ProofreadChangesDialog", "Review"),
        .manualPair = QCoreApplication::translate("ProofreadChangesDialog", "Manual pair"),
        .reviewStates = {QCoreApplication::translate("ProofreadChangesDialog", "Unseen"),
                         QCoreApplication::translate("ProofreadChangesDialog", "Reviewed"),
                         QCoreApplication::translate("ProofreadChangesDialog", "Needs confirmation")},
        .comment = QCoreApplication::translate("ProofreadChangesDialog", "Comment"),
    };
}

QString ProofreadReportService::htmlReport(const QVector<ReviewChange>& changes,
                                           const labelqt::core::Project& beforeProject,
                                           const labelqt::core::Project& currentProject,
                                           const ProofreadReportTexts& texts, const QString& filterDescription,
                                           ProofreadReportOptions options)
{
    QString html = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"/>"
                                  "<title>%1</title><style>%2</style></head><body>")
                       .arg(escaped(texts.title), reportStyle());
    html += QStringLiteral("<h1>%1</h1>").arg(escaped(texts.title));
    html += QStringLiteral("<div class=\"meta\">%1: %2<br/>%3: %4</div>")
                .arg(escaped(texts.generatedAt), escaped(QDateTime::currentDateTime().toString(Qt::ISODate)),
                     escaped(texts.totalChanges))
                .arg(changes.size());
    if (!filterDescription.isEmpty()) {
        html +=
            QStringLiteral("<div class=\"meta\">%1: %2</div>").arg(escaped(texts.filter), escaped(filterDescription));
    }

    for (const auto& changeIndexes : groupedChanges(changes)) {
        html += pageSectionHtml(changes.at(changeIndexes.first()).pageName(), changeIndexes, changes, beforeProject,
                                currentProject, texts, options);
    }

    html += QStringLiteral("</body></html>");
    return html;
}

std::expected<void, QString>
ProofreadReportService::saveHtmlReport(const QString& filePath, const QVector<ReviewChange>& changes,
                                       const labelqt::core::Project& beforeProject,
                                       const labelqt::core::Project& currentProject, const ProofreadReportTexts& texts,
                                       const QString& filterDescription, ProofreadReportOptions options)
{
    return labelqt::core::AtomicFileWriter::write(
        filePath, htmlReport(changes, beforeProject, currentProject, texts, filterDescription, options).toUtf8());
}

} // namespace labelqt::services
