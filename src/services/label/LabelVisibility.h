#pragma once

#include "core/Label.h"

#include <QSet>
#include <QVector>

namespace labelqt::services {

// Display numbers are positions in this projection plus one, never storage indexes or stable IDs.
// This view filter must not be used to select labels for persistence.
class LabelVisibility final {
public:
    static bool isVisible(const core::Label& label, const QSet<QString>& groups);
    static QVector<int> sourceIndexes(const QVector<core::Label>& labels, const QSet<QString>& groups);
    static int displayNumber(const QVector<core::Label>& labels, const QSet<QString>& groups, int sourceIndex);
};

} // namespace labelqt::services
