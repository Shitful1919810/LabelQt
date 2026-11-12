#pragma once

#include "services/proofread/ProofreadReportService.h"
#include <QImage>

// Shared report semantics, independent of HTML/PDF layout and frontend widgets.
namespace labelqt::services::report {
struct Marker {
    QPointF position;
    QString number;
    ReviewChangeKind kind;
};
QString escaped(QString text);
QString labelNumber(int labelIndex);
QString changeKindText(const ReviewChange& change, const ProofreadReportTexts& texts);
QString changeSummary(const ReviewChange& change, const ProofreadReportTexts& texts);
QString detailsHtml(const ReviewChange& change, const ProofreadReportTexts& texts,
                    const ProofreadReportOptions& options);
QVector<QVector<int>> groupedChanges(const QVector<ReviewChange>& changes);
QPair<const core::ImageEntry*, const core::ImageEntry*> imagesForChanges(const QVector<int>& indexes,
                                                                         const QVector<ReviewChange>& changes,
                                                                         const core::Project& before,
                                                                         const core::Project& after);
QVector<Marker> markersForChanges(const QVector<int>& indexes, const QVector<ReviewChange>& changes, bool before);
QImage loadImage(const QString& path, const ProofreadReportOptions& options);
} // namespace labelqt::services::report
