#pragma once

#include "services/proofread/ReviewMetadataService.h"
#include <QJsonObject>
#include <expected>

namespace labelqt::services {
enum class ReviewStatus { Unseen, Reviewed, NeedsConfirmation };
struct ReviewAnnotation {
    ReviewStatus status{ReviewStatus::Unseen};
    QString note;
};
struct ManualLabelPair {
    ReviewLabelSnapshot baseline;
    ReviewLabelSnapshot current;
};
struct ReviewSession {
    QHash<QString, ReviewAnnotation> annotations;
    QVector<ManualLabelPair> pairs;
};
class ReviewSessionService final {
public:
    static QString fingerprint(const ReviewChange& change);
    static QString snapshotFingerprint(const ReviewLabelSnapshot& snapshot);
    static QString contextKey(const labelqt::core::Project& before, const labelqt::core::Project& current);
    static QJsonObject encode(const ReviewSession& session);
    static std::expected<ReviewSession, QString> decode(const QJsonObject& object);
    static std::expected<ReviewSession, QString> loadLocal(const QString& key);
    static std::expected<void, QString> saveLocal(const QString& key, const ReviewSession& session);
    static QJsonObject projectSession(const labelqt::core::Project& project);
    static QStringList withProjectSession(const QStringList& comments, const QJsonObject& session);
};
} // namespace labelqt::services
