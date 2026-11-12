#pragma once

#include "core/UndoStack.h"
#include "services/project/ProjectController.h"
#include "services/proofread/ReviewRoundService.h"

namespace labelqt::services {

struct ReviewEditToken {
    QUuid session;
    quint64 revision;
};

struct ReviewNoteEdit {
    enum class Operation { Add, Edit, Remove, Resolve, Relink };
    Operation operation;
    QUuid noteId{};
    QUuid pageId{};
    QUuid labelId{};
    QString text{};
    bool resolved{false};
};

class ReviewRoundEditService final {
public:
    static ReviewEditToken token(ProjectController& controller);
    // Mutates only a detached candidate; commit validates and publishes it atomically with undo.
    static std::expected<void, QString> editNote(ReviewRound& round, const labelqt::core::Project& project,
                                                 const ReviewNoteEdit& edit);
    static std::expected<void, QString> commit(ProjectController& controller, labelqt::core::UndoStack& undo,
                                               ReviewEditToken token, const std::optional<ReviewRound>& round,
                                               const QString& description);
};
} // namespace labelqt::services
