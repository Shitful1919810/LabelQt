#pragma once

#include "core/UndoStack.h"
#include "services/project/ProjectController.h"
#include "services/proofread/ReviewRoundService.h"

namespace labelqt::services {

struct ReviewEditToken {
    QUuid session;
    quint64 revision;
};

class ReviewRoundEditService final {
public:
    static ReviewEditToken token(ProjectController& controller);
    static std::expected<void, QString> commit(ProjectController& controller, labelqt::core::UndoStack& undo,
                                               ReviewEditToken token, const std::optional<ReviewRound>& round,
                                               const QString& description);
};
} // namespace labelqt::services
