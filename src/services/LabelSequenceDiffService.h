#pragma once

#include "services/ReviewMetadataService.h"

#include <QVector>

namespace labelqt::services {

struct LabelSequenceDiffEntry {
    int baselineLabelIndex{-1};
    int currentLabelIndex{-1};
    ReviewLabelSnapshot baseline;
    ReviewLabelSnapshot current;
    bool moved{false};
};

class LabelSequenceDiffService final {
public:
    static QVector<LabelSequenceDiffEntry> diff(QVector<ReviewLabelSnapshot> baselineLabels,
                                                QVector<ReviewLabelSnapshot> currentLabels);
};

} // namespace labelqt::services
