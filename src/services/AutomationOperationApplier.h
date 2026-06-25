#pragma once

#include "core/Project.h"
#include "services/AutomationService.h"

#include <QString>
#include <QVector>

namespace labelqt::services {

struct AutomationLabelGroupChange {
    int imageIndex{-1};
    int labelIndex{-1};
    QString oldGroup;
    QString newGroup;
};

struct AutomationLabelAddition {
    int imageIndex{-1};
    int labelIndex{-1};
    labelqt::core::Label label;
};

struct AutomationLabelTextChange {
    int imageIndex{-1};
    int labelIndex{-1};
    QString oldText;
    QString newText;
};

struct AutomationLabelPositionChange {
    int imageIndex{-1};
    int labelIndex{-1};
    QPointF oldPosition;
    QPointF newPosition;
};

struct AutomationLabelDeletedChange {
    int imageIndex{-1};
    int labelIndex{-1};
    bool oldDeleted{false};
    bool newDeleted{false};
};

struct AutomationOperationApplyPlan {
    QVector<AutomationLabelGroupChange> labelGroupChanges;
    QVector<AutomationLabelAddition> labelAdditions;
    QVector<AutomationLabelTextChange> labelTextChanges;
    QVector<AutomationLabelPositionChange> labelPositionChanges;
    QVector<AutomationLabelDeletedChange> labelDeletedChanges;
    int ignoredOperationCount{0};

    bool hasChanges() const noexcept;
    int changeCount() const noexcept;
};

class AutomationOperationApplier final {
public:
    static AutomationOperationApplyPlan plan(const labelqt::core::Project& project,
                                             const QVector<AutomationOperation>& operations);
    static void apply(labelqt::core::Project& project, const AutomationOperationApplyPlan& plan, bool redo);
};

} // namespace labelqt::services
