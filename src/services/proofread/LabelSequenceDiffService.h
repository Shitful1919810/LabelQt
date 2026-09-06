#pragma once

#include "services/proofread/ReviewMetadataService.h"

#include <QVector>

namespace labelqt::services {

struct LabelSequenceDiffEntry {
    int baselineLabelIndex{-1};
    int currentLabelIndex{-1};
    ReviewLabelSnapshot baseline;
    ReviewLabelSnapshot current;
    bool moved{false};
    bool manuallyMatched{false};
};

class LabelSequenceDiffService final {
public:
    static QVector<LabelSequenceDiffEntry> diff(QVector<ReviewLabelSnapshot> baselineLabels,
                                                QVector<ReviewLabelSnapshot> currentLabels);
    static QVector<LabelSequenceDiffEntry> diffWithPairs(QVector<ReviewLabelSnapshot> baselineLabels,
                                                         QVector<ReviewLabelSnapshot> currentLabels,
                                                         const QVector<QPair<int, int>>& pairs);
};

} // namespace labelqt::services
