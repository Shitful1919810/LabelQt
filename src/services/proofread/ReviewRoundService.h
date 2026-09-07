#pragma once

#include "services/proofread/ReviewSessionService.h"

#include <optional>

namespace labelqt::services {
class ProjectController;

struct ProofreadNote {
    QUuid id{QUuid::createUuid()};
    QUuid pageId;
    QUuid labelId;
    QString pageName;
    QString quote;
    QString text;
    bool resolved{false};
    QUuid associatedPageId;
    QUuid associatedLabelId;
};

struct ReviewRound {
    QUuid id{QUuid::createUuid()};
    labelqt::core::Project before;
    std::optional<labelqt::core::Project> after;
    QVector<ProofreadNote> notes;
    QJsonObject session;
};

struct ReviewRoundPresentation {
    labelqt::core::Project before;
    labelqt::core::Project after;
    QVector<ReviewChange> changes;
    bool uncertainIdentity{false};
};

class ReviewRoundService final {
public:
    static labelqt::core::Project snapshot(const labelqt::core::Project& project);
    static ReviewRound complete(ReviewRound round, const labelqt::core::Project& project);
    static std::expected<std::optional<ReviewRound>, QString> read(const labelqt::core::Project& project);
    static QJsonObject encode(const ReviewRound& round, const QString& projectPath);
    static ReviewRoundPresentation present(const ReviewRound& round, const labelqt::core::Project& live);
    static QPair<int, int> locate(const labelqt::core::Project& project, const QUuid& pageId, const QUuid& labelId);
    static QPair<int, int> locateNote(const labelqt::core::Project& project, const ProofreadNote& note);
    static QString noteText(const ProofreadNote& note);
    static QJsonObject rebase(const QJsonObject& section, const QString& fromPath, const QString& toPath);
};
} // namespace labelqt::services
