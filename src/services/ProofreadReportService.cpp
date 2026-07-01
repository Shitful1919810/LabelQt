#include "services/ProofreadReportService.h"

#include "services/ReviewChangeClassifier.h"
#include "services/TextDiffHtmlRenderer.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {

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
        return TextDiffHtmlRenderer::renderInlineDiff(beforeText, afterText, texts.noTextChange);
    }

    return escaped(texts.noTextChange);
}

const labelqt::core::ImageEntry* imageByName(const labelqt::core::Project& project, const QString& imageName)
{
    const auto it = std::ranges::find_if(project.images(), [&imageName](const labelqt::core::ImageEntry& image) {
        return image.name == imageName;
    });
    return it == project.images().cend() ? nullptr : &(*it);
}

QString pageNameForChange(const ReviewChange& change)
{
    return change.imageName.isEmpty() ? change.baselineImageName : change.imageName;
}

QString compressedImageDataUrl(const QString& imagePath, const ProofreadReportOptions& options)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && options.maxImageWidth > 0 && sourceSize.width() > options.maxImageWidth) {
        reader.setScaledSize(sourceSize.scaled(options.maxImageWidth, options.maxImageWidth, Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
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

QSize imageSize(const QString& imagePath)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    return size.isValid() ? size : QSize(4, 3);
}

ReportImage reportImageForEntry(const labelqt::core::ImageEntry* image, const ProofreadReportOptions& options)
{
    if (image == nullptr) {
        return {};
    }
    return {.source = compressedImageDataUrl(image->path, options), .size = imageSize(image->path)};
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

QVector<QString> pageOrderForChanges(const QVector<ReviewChange>& changes)
{
    QVector<QString> pageNames;
    QSet<QString> seen;
    pageNames.reserve(changes.size());
    for (const ReviewChange& change : changes) {
        const QString pageName = pageNameForChange(change);
        if (pageName.isEmpty() || seen.contains(pageName)) {
            continue;
        }
        seen.insert(pageName);
        pageNames.append(pageName);
    }
    return pageNames;
}

QString pageSectionHtml(const QString& pageName, const QVector<int>& changeIndexes, const QVector<ReviewChange>& changes,
                        const labelqt::core::Project& beforeProject, const labelqt::core::Project& currentProject,
                        const ProofreadReportTexts& texts, const ProofreadReportOptions& options)
{
    const labelqt::core::ImageEntry* beforeImage = nullptr;
    const labelqt::core::ImageEntry* afterImage = nullptr;
    for (const int changeIndex : changeIndexes) {
        const ReviewChange& change = changes.at(changeIndex);
        if (beforeImage == nullptr) {
            beforeImage = imageByName(beforeProject, change.baselineImageName.isEmpty() ? change.imageName
                                                                                       : change.baselineImageName);
        }
        if (afterImage == nullptr) {
            afterImage = imageByName(currentProject, change.imageName.isEmpty() ? change.baselineImageName
                                                                                : change.imageName);
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
        for (const int changeIndex : changeIndexes) {
            const ReviewChange& change = changes.at(changeIndex);
            if (change.kind == ReviewChangeKind::Added) {
                continue;
            }
            const QPointF position = clampedPosition(change.baseline.position);
            html += QStringLiteral("<span class=\"marker %1\" style=\"left:%2%;top:%3%\">%4</span>")
                        .arg(markerClass(change.kind == ReviewChangeKind::Deleted ? ReviewChangeKind::Deleted
                                                                                   : ReviewChangeKind::Modified))
                        .arg(position.x() * 100.0, 0, 'f', 2)
                        .arg(position.y() * 100.0, 0, 'f', 2)
                        .arg(escaped(labelNumber(beforeLabelIndex(change))));
        }
        html += QStringLiteral("</div></div><div class=\"image-frame\"><h3>%1</h3>"
                               "<div class=\"image-overlay image-after\">")
                    .arg(escaped(texts.after));
        for (const int changeIndex : changeIndexes) {
            const ReviewChange& change = changes.at(changeIndex);
            if (change.kind == ReviewChangeKind::Deleted) {
                continue;
            }
            const QPointF position = clampedPosition(change.current.position);
            html += QStringLiteral("<span class=\"marker %1\" style=\"left:%2%;top:%3%\">%4</span>")
                        .arg(markerClass(change.kind == ReviewChangeKind::Added ? ReviewChangeKind::Added
                                                                                 : ReviewChangeKind::Modified))
                        .arg(position.x() * 100.0, 0, 'f', 2)
                        .arg(position.y() * 100.0, 0, 'f', 2)
                        .arg(escaped(labelNumber(afterLabelIndex(change))));
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
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                    .arg(escaped(labelNumber(change.labelIndex)), escaped(changeKindText(change, texts)),
                         escaped(changeSummary(change, texts)), textDiffHtml(change, texts));
    }
    html += QStringLiteral("</tbody></table></section>");
    return html;
}

} // namespace

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
                .arg(escaped(texts.generatedAt),
                     escaped(QDateTime::currentDateTime().toString(Qt::ISODate)),
                     escaped(texts.totalChanges))
                .arg(changes.size());
    if (!filterDescription.isEmpty()) {
        html += QStringLiteral("<div class=\"meta\">%1: %2</div>")
                    .arg(escaped(texts.filter), escaped(filterDescription));
    }

    const QVector<QString> pageNames = pageOrderForChanges(changes);
    for (const QString& pageName : pageNames) {
        QVector<int> changeIndexes;
        for (int index = 0; index < changes.size(); ++index) {
            if (pageNameForChange(changes.at(index)) == pageName) {
                changeIndexes.append(index);
            }
        }
        html += pageSectionHtml(pageName, changeIndexes, changes, beforeProject, currentProject, texts, options);
    }

    html += QStringLiteral("</body></html>");
    return html;
}

std::expected<void, QString> ProofreadReportService::saveHtmlReport(const QString& filePath,
                                                                    const QVector<ReviewChange>& changes,
                                                                    const labelqt::core::Project& beforeProject,
                                                                    const labelqt::core::Project& currentProject,
                                                                    const ProofreadReportTexts& texts,
                                                                    const QString& filterDescription,
                                                                    ProofreadReportOptions options)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return std::unexpected(file.errorString());
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << htmlReport(changes, beforeProject, currentProject, texts, filterDescription, options);
    return {};
}

} // namespace labelqt::services
