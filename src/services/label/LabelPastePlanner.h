#pragma once

#include "core/Project.h"

#include <optional>

#include <QSet>
#include <QVector>

namespace labelqt::services {

class LabelPastePlanner {
public:
    enum class PasteBehavior {
        OffsetOnPaste,
        PreservePositionOnPaste,
    };

    struct PasteOptions {
        PasteBehavior behavior{PasteBehavior::OffsetOnPaste};
        std::optional<QPointF> anchorPosition;
    };

    static QVector<labelqt::core::Label> labelsAdjustedForPaste(const QVector<labelqt::core::Label>& labels,
                                                                const labelqt::core::ImageEntry& image,
                                                                const QSet<QString>& visibleGroups,
                                                                PasteOptions options);
};

} // namespace labelqt::services
