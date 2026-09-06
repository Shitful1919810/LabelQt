#pragma once

#include <QPointF>
#include <QString>
#include <QUuid>
#include <QVector>
#include <optional>

namespace labelqt::services {

// Null labelId creates a label. Fields not present in a change are never overwritten, including during undo.
struct LabelChange {
    QUuid pageId;
    QUuid labelId;
    std::optional<QString> text;
    std::optional<QUuid> groupId;
    std::optional<QPointF> position;
    std::optional<bool> deleted;
};

enum class LabelCommitError { InvalidIdentity, InvalidChange, WrongThread };

struct LabelCommitReceipt {
    QVector<QUuid> changedLabels;
    QVector<QUuid> createdLabels;
};

} // namespace labelqt::services
