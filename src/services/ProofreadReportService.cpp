#include "services/ProofreadReportService.h"

#include "services/TextDiffHtmlRenderer.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <utility>

namespace labelqt::services {
namespace {

QString escaped(QString text)
{
    return TextDiffHtmlRenderer::escapedText(std::move(text));
}

QString labelNumber(int labelIndex)
{
    return labelIndex < 0 ? QStringLiteral("-")
                          : QString::number(labelIndex + 1).rightJustified(3, QLatin1Char('0'));
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
    if (change.textChanged) {
        parts.append(texts.text);
    }
    if (change.groupChanged) {
        parts.append(texts.group);
    }
    if (change.positionChanged) {
        parts.append(texts.marker);
    }
    if (change.orderChanged) {
        parts.append(texts.order);
    }
    return parts.isEmpty() ? texts.modified : parts.join(QStringLiteral(", "));
}

QString coordinateText(QPointF position)
{
    return QStringLiteral("(%1, %2)")
        .arg(position.x(), 0, 'f', 4)
        .arg(position.y(), 0, 'f', 4);
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

QString detailSummaryHtml(const ReviewChange& change, const ProofreadReportTexts& texts)
{
    QStringList parts;
    if (change.groupChanged) {
        parts.append(QStringLiteral("%1: %2 -> %3")
                         .arg(escaped(texts.groupChange), escaped(change.baseline.group), escaped(change.current.group)));
    }
    if (change.positionChanged) {
        parts.append(QStringLiteral("%1: %2 -> %3")
                         .arg(escaped(texts.markerChange), escaped(coordinateText(change.baseline.position)),
                              escaped(coordinateText(change.current.position))));
    }
    if (change.orderChanged) {
        parts.append(QStringLiteral("%1: %2 -> %3")
                         .arg(escaped(texts.orderChange), escaped(labelNumber(change.baselineLabelIndex)),
                              escaped(labelNumber(change.currentLabelIndex))));
    }
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("<br/>"));
}

QString reportStyle()
{
    return QStringLiteral(R"(
body { font-family: sans-serif; line-height: 1.35; margin: 24px; color: #1f2937; background: #ffffff; }
h1 { font-size: 28px; margin-bottom: 4px; }
.meta { color: #6b7280; margin-bottom: 14px; }
table { border-collapse: collapse; width: 100%; table-layout: fixed; }
th, td { border: 1px solid #d1d5db; padding: 6px 8px; vertical-align: top; overflow-wrap: anywhere; }
th { background: #f3f4f6; color: #374151; text-align: left; }
tr:nth-child(even) { background: #fafafa; }
.column-index { width: 48px; }
.column-page { width: 130px; }
.column-label { width: 72px; }
.column-type { width: 90px; }
.column-summary { width: 120px; }
p { margin: 0; }
)");
}

} // namespace

QString ProofreadReportService::htmlReport(const QVector<ReviewChange>& changes, const ProofreadReportTexts& texts,
                                           const QString& sourceDescription)
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
    if (!sourceDescription.isEmpty()) {
        html += QStringLiteral("<div class=\"meta\">%1</div>").arg(escaped(sourceDescription));
    }

    html += QStringLiteral("<table><thead><tr>"
                           "<th class=\"column-index\">#</th>"
                           "<th class=\"column-page\">%1</th>"
                           "<th class=\"column-label\">%2</th>"
                           "<th class=\"column-type\">%3</th>"
                           "<th class=\"column-summary\">%4</th>"
                           "<th>%5</th>"
                           "<th>%6</th>"
                           "</tr></thead><tbody>")
                .arg(escaped(texts.page), escaped(texts.label), escaped(texts.changeType), escaped(texts.summary),
                     escaped(texts.textDifference), escaped(QStringLiteral("%1 / %2").arg(texts.before, texts.after)));
    for (int index = 0; index < changes.size(); ++index) {
        const ReviewChange& change = changes.at(index);
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
                    .arg(index + 1)
                    .arg(escaped(change.imageName), escaped(labelNumber(change.labelIndex)),
                         escaped(changeKindText(change, texts)), escaped(changeSummary(change, texts)),
                         textDiffHtml(change, texts), detailSummaryHtml(change, texts));
    }
    html += QStringLiteral("</tbody></table>");

    html += QStringLiteral("</body></html>");
    return html;
}

std::expected<void, QString> ProofreadReportService::saveHtmlReport(const QString& filePath,
                                                                    const QVector<ReviewChange>& changes,
                                                                    const ProofreadReportTexts& texts,
                                                                    const QString& sourceDescription)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return std::unexpected(file.errorString());
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << htmlReport(changes, texts, sourceDescription);
    return {};
}

} // namespace labelqt::services
