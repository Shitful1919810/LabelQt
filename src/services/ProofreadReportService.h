#pragma once

#include "services/ReviewMetadataService.h"

#include <QString>
#include <QVector>

#include <expected>

namespace labelqt::services {

struct ProofreadReportTexts {
    QString title;
    QString generatedAt;
    QString totalChanges;
    QString page;
    QString label;
    QString changeType;
    QString summary;
    QString textDifference;
    QString groupChange;
    QString markerChange;
    QString orderChange;
    QString before;
    QString after;
    QString added;
    QString deleted;
    QString modified;
    QString text;
    QString group;
    QString marker;
    QString order;
    QString noTextChange;
};

class ProofreadReportService final {
public:
    static QString htmlReport(const QVector<ReviewChange>& changes, const ProofreadReportTexts& texts,
                              const QString& sourceDescription = {});
    static std::expected<void, QString> saveHtmlReport(const QString& filePath, const QVector<ReviewChange>& changes,
                                                       const ProofreadReportTexts& texts,
                                                       const QString& sourceDescription = {});
};

} // namespace labelqt::services
