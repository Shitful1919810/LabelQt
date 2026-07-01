#pragma once

#include "core/Project.h"
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
    QString format;
    QString group;
    QString marker;
    QString order;
    QString noTextChange;
    QString filter;
};

struct ProofreadReportOptions {
    int maxImageWidth{960};
    int jpegQuality{62};
};

class ProofreadReportService final {
public:
    static QString htmlReport(const QVector<ReviewChange>& changes, const labelqt::core::Project& beforeProject,
                              const labelqt::core::Project& currentProject, const ProofreadReportTexts& texts,
                              const QString& filterDescription = {}, ProofreadReportOptions options = {});
    static std::expected<void, QString> saveHtmlReport(const QString& filePath, const QVector<ReviewChange>& changes,
                                                       const labelqt::core::Project& beforeProject,
                                                       const labelqt::core::Project& currentProject,
                                                       const ProofreadReportTexts& texts,
                                                       const QString& filterDescription = {},
                                                       ProofreadReportOptions options = {});
};

} // namespace labelqt::services
