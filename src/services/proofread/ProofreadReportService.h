#pragma once

#include "core/Project.h"
#include "services/proofread/ReviewMetadataService.h"
#include "services/proofread/ReviewSessionService.h"

#include <QString>
#include <QVector>

#include <expected>
#include <functional>

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
    QString review;
    QString manualPair;
    QStringList reviewStates;
    QString comment{};
};

struct ProofreadReportOptions {
    int maxImageWidth{960};
    int jpegQuality{62};
    bool includeReview{false};
    QHash<QString, ReviewAnnotation> annotations;
};

class ProofreadReportService final {
public:
    static ProofreadReportTexts defaultTexts(const QString& before, const QString& after);
    static QString htmlReport(const QVector<ReviewChange>& changes, const labelqt::core::Project& beforeProject,
                              const labelqt::core::Project& currentProject, const ProofreadReportTexts& texts,
                              const QString& filterDescription = {}, ProofreadReportOptions options = {});
    static std::expected<void, QString> saveHtmlReport(const QString& filePath, const QVector<ReviewChange>& changes,
                                                       const labelqt::core::Project& beforeProject,
                                                       const labelqt::core::Project& currentProject,
                                                       const ProofreadReportTexts& texts,
                                                       const QString& filterDescription = {},
                                                       ProofreadReportOptions options = {});
    static std::expected<void, QString>
    savePdfReport(const QString& filePath, const QVector<ReviewChange>& changes,
                  const labelqt::core::Project& beforeProject, const labelqt::core::Project& currentProject,
                  const ProofreadReportTexts& texts, const QString& filterDescription = {},
                  ProofreadReportOptions options = {}, const std::function<bool()>& canceled = {});
};

} // namespace labelqt::services
