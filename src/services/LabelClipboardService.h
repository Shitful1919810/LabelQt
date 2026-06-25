#pragma once

#include "core/Label.h"

#include <QString>
#include <QVector>

class QMimeData;

namespace labelqt::services {

struct ClipboardLabels {
    enum class PasteBehavior {
        OffsetOnPaste,
        PreservePositionOnPaste,
    };

    QString sourceImageName;
    QVector<labelqt::core::Label> labels;
    PasteBehavior pasteBehavior{PasteBehavior::OffsetOnPaste};
};

class LabelClipboardService {
public:
    static constexpr const char* mimeType = "application/x-labelqt-labels";

    static QMimeData* createMimeData(const ClipboardLabels& labels);
    static ClipboardLabels readMimeData(const QMimeData* mimeData);
};

} // namespace labelqt::services
