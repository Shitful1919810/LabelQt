#include "services/proofread/ProofreadReportContent.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextTable>
#include <algorithm>
#include <stdexcept>

namespace labelqt::services {
namespace {
void checkCanceled(const std::function<bool()>& canceled)
{
    if (canceled && canceled())
        throw std::runtime_error(
            QCoreApplication::translate("ProofreadPdfReport", "Report export canceled.").toStdString());
}

void insertPreview(QTextCursor cursor, QTextDocument& document, const QString& key, const core::ImageEntry* entry,
                   const QVector<report::Marker>& markers, const ProofreadReportOptions& options, qreal width)
{
    auto image = entry ? report::loadImage(entry->path, options) : QImage{};
    if (image.isNull()) {
        cursor.insertText(QCoreApplication::translate("ProofreadPdfReport", "Image unavailable"));
        return;
    }
    image = image.convertToFormat(QImage::Format_RGB32);
    const QSizeF display = QSizeF(image.size()).scaled(width, 220, Qt::KeepAspectRatio);
    // Composite markers into the preview only; report text remains searchable PDF text.
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal scale = image.width() / display.width();
    QFont font;
    font.setPixelSize(std::max(1, qRound(8 * scale)));
    font.setBold(true);
    painter.setFont(font);
    for (const auto& marker : markers) {
        const qreal w =
            std::min(qreal(image.width()), painter.fontMetrics().horizontalAdvance(marker.number) + 6 * scale);
        const qreal h = std::min(qreal(image.height()), 13 * scale);
        QRectF rect(std::clamp(marker.position.x() * image.width() - w / 2, 0.0, image.width() - w),
                    std::clamp(marker.position.y() * image.height() - h / 2, 0.0, image.height() - h), w, h);
        painter.setBrush(marker.kind == ReviewChangeKind::Added     ? QColor("#15803d")
                         : marker.kind == ReviewChangeKind::Deleted ? QColor("#b91c1c")
                                                                    : QColor("#ca8a04"));
        painter.setPen(QPen(Qt::white, scale));
        painter.drawRoundedRect(rect, 3 * scale, 3 * scale);
        painter.drawText(rect, Qt::AlignCenter, marker.number);
    }
    painter.end();
    const QUrl url("report:" + key);
    document.addResource(QTextDocument::ImageResource, url, image);
    QTextImageFormat format;
    format.setName(url.toString());
    format.setWidth(display.width());
    format.setHeight(display.height());
    cursor.insertImage(format);
}

QTextTableFormat tableFormat(const QList<qreal>& widths)
{
    QTextTableFormat format;
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QList<QTextLength> columns;
    for (const auto width : widths)
        columns.append(QTextLength(QTextLength::PercentageLength, width));
    format.setColumnWidthConstraints(columns);
    format.setBorder(0.5);
    format.setBorderBrush(QColor("#b5bec6"));
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    format.setCellPadding(4);
    format.setCellSpacing(0);
    format.setHeaderRowCount(1);
    return format;
}
} // namespace

std::expected<void, QString>
ProofreadReportService::savePdfReport(const QString& path, const QVector<ReviewChange>& changes,
                                      const core::Project& before, const core::Project& after,
                                      const ProofreadReportTexts& texts, const QString& filters,
                                      ProofreadReportOptions options, const std::function<bool()>& canceled)
{
    try {
        checkCanceled(canceled);
        if (changes.isEmpty())
            return std::unexpected(
                QCoreApplication::translate("ProofreadChangesDialog", "No proofreading changes to export."));
        if (changes.size() > 100000)
            return std::unexpected(
                QCoreApplication::translate("ProofreadPdfReport", "Too many changes. Export a smaller page range."));
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            return std::unexpected(file.errorString());
        // End/destroy the PDF device before committing: its trailer is part of the transaction.
        {
            QPdfWriter writer(&file);
            writer.setCreator("LabelQt");
            writer.setTitle(texts.title);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
            QPainter painter(&writer);
            if (!painter.isActive())
                return std::unexpected(
                    QCoreApplication::translate("ProofreadPdfReport", "Cannot initialize PDF output."));
            const QSizeF size(writer.width(), writer.height() - 20);
            int sheet = 0;
            const QString generated = QDateTime::currentDateTime().toString(Qt::ISODate);
            options.maxImageWidth = std::clamp(options.maxImageWidth, 320, 2048);
            // One document per image pair bounds decoded image memory independently of project length.
            for (const auto& indexes : report::groupedChanges(changes)) {
                checkCanceled(canceled);
                QTextDocument document;
                document.setLayoutEnabled(false);
                document.documentLayout()->setPaintDevice(&writer);
                document.setDefaultFont(QFont("sans-serif", 9));
                auto textOptions = document.defaultTextOption();
                textOptions.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                document.setDefaultTextOption(textOptions);
                document.setDocumentMargin(2);
                document.setPageSize(size);
                QTextCursor cursor(&document);
                if (sheet == 0) {
                    cursor.insertHtml(QStringLiteral("<h1>%1</h1><p>%2: %3<br>%4: %5</p>")
                                          .arg(report::escaped(texts.title), report::escaped(texts.generatedAt),
                                               report::escaped(generated), report::escaped(texts.totalChanges))
                                          .arg(changes.size()));
                    if (!filters.isEmpty()) {
                        cursor.insertBlock();
                        cursor.insertHtml(QStringLiteral("<p>%1: %2</p>")
                                              .arg(report::escaped(texts.filter), report::escaped(filters)));
                    }
                    cursor.insertBlock();
                }
                cursor.insertHtml(
                    QStringLiteral("<h2>%1</h2>").arg(report::escaped(changes.at(indexes.first()).pageName())));
                cursor.insertBlock();
                auto* previews = cursor.insertTable(2, 2, tableFormat({50, 50}));
                previews->cellAt(0, 0).firstCursorPosition().insertText(texts.before);
                previews->cellAt(0, 1).firstCursorPosition().insertText(texts.after);
                const auto [left, right] = report::imagesForChanges(indexes, changes, before, after);
                insertPreview(previews->cellAt(1, 0).firstCursorPosition(), document, "before", left,
                              report::markersForChanges(indexes, changes, true), options, size.width() / 2 - 16);
                checkCanceled(canceled);
                insertPreview(previews->cellAt(1, 1).firstCursorPosition(), document, "after", right,
                              report::markersForChanges(indexes, changes, false), options, size.width() / 2 - 16);
                cursor.movePosition(QTextCursor::End);
                cursor.insertBlock();
                auto* table = cursor.insertTable(static_cast<int>(indexes.size()) + 1, 4, tableFormat({8, 12, 17, 63}));
                const QStringList headers{texts.label, texts.changeType, texts.summary, texts.textDifference};
                for (int column = 0; column < 4; ++column) {
                    auto cell = table->cellAt(0, column);
                    auto format = cell.format();
                    format.setBackground(QColor("#eef1f3"));
                    format.setFontWeight(QFont::Bold);
                    cell.setFormat(format);
                    cell.firstCursorPosition().insertText(headers.at(column));
                }
                for (int i = 0; i < indexes.size(); ++i) {
                    checkCanceled(canceled);
                    const auto& change = changes.at(indexes.at(i));
                    table->cellAt(i + 1, 0).firstCursorPosition().insertText(report::labelNumber(change.labelIndex));
                    table->cellAt(i + 1, 1).firstCursorPosition().insertText(report::changeKindText(change, texts));
                    table->cellAt(i + 1, 2).firstCursorPosition().insertText(report::changeSummary(change, texts));
                    table->cellAt(i + 1, 3).firstCursorPosition().insertHtml(
                        report::detailsHtml(change, texts, options));
                }
                document.setLayoutEnabled(true);
                const int pages = document.pageCount();
                for (int page = 0; page < pages; ++page) {
                    checkCanceled(canceled);
                    if (sheet > 0 && !writer.newPage())
                        return std::unexpected(
                            QCoreApplication::translate("ProofreadPdfReport", "Cannot write PDF page."));
                    ++sheet;
                    painter.save();
                    painter.setClipRect(QRectF(QPointF(0, 0), size));
                    painter.translate(0, -page * size.height());
                    document.drawContents(&painter, QRectF(0, page * size.height(), size.width(), size.height()));
                    painter.restore();
                    painter.setFont(QFont("sans-serif", 8));
                    painter.setPen(QColor("#667078"));
                    painter.drawText(QRectF(0, size.height() + 4, size.width(), 16), Qt::AlignRight,
                                     QString::number(sheet));
                }
            }
            if (!painter.end())
                return std::unexpected(QCoreApplication::translate("ProofreadPdfReport", "Cannot finish PDF output."));
        }
        checkCanceled(canceled);
        if (file.error() != QFileDevice::NoError || !file.commit())
            return std::unexpected(file.errorString());
        return {};
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}
} // namespace labelqt::services
