#include "services/LabelPastePlanner.h"

#include <algorithm>
#include <cmath>

namespace labelqt::services {

namespace {
constexpr double pastedLabelOffset = 0.035;
constexpr double labelCollisionDistance = 0.025;

QPointF boundedShiftForLabels(const QVector<labelqt::core::Label>& labels, QPointF requestedShift)
{
    if (labels.isEmpty()) {
        return {};
    }

    double minX = 1.0;
    double minY = 1.0;
    double maxX = 0.0;
    double maxY = 0.0;
    for (const labelqt::core::Label& label : labels) {
        const QPointF position = label.position();
        minX = std::min(minX, position.x());
        minY = std::min(minY, position.y());
        maxX = std::max(maxX, position.x());
        maxY = std::max(maxY, position.y());
    }

    return QPointF(std::clamp(requestedShift.x(), -minX, 1.0 - maxX),
                   std::clamp(requestedShift.y(), -minY, 1.0 - maxY));
}

QVector<labelqt::core::Label> shiftedLabels(QVector<labelqt::core::Label> labels, QPointF requestedShift)
{
    const QPointF shift = boundedShiftForLabels(labels, requestedShift);
    for (labelqt::core::Label& label : labels) {
        label.setPosition(label.position() + shift);
    }
    return labels;
}

QPointF labelGroupCenter(const QVector<labelqt::core::Label>& labels)
{
    if (labels.isEmpty()) {
        return {};
    }

    double minX = 1.0;
    double minY = 1.0;
    double maxX = 0.0;
    double maxY = 0.0;
    for (const labelqt::core::Label& label : labels) {
        const QPointF position = label.position();
        minX = std::min(minX, position.x());
        minY = std::min(minY, position.y());
        maxX = std::max(maxX, position.x());
        maxY = std::max(maxY, position.y());
    }

    return QPointF((minX + maxX) / 2.0, (minY + maxY) / 2.0);
}

bool labelsCollideWithExistingLabels(const QVector<labelqt::core::Label>& labels,
                                     const labelqt::core::ImageEntry& image,
                                     const QSet<QString>& visibleGroups)
{
    for (const labelqt::core::Label& pastedLabel : labels) {
        const QPointF pastedPosition = pastedLabel.position();
        for (const labelqt::core::Label& existingLabel : image.labels) {
            if (existingLabel.isDeleted() || !visibleGroups.contains(existingLabel.group())) {
                continue;
            }
            const QPointF delta = pastedPosition - existingLabel.position();
            if (std::hypot(delta.x(), delta.y()) < labelCollisionDistance) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

QVector<labelqt::core::Label> LabelPastePlanner::labelsAdjustedForPaste(
    const QVector<labelqt::core::Label>& labels, const labelqt::core::ImageEntry& image,
    const QSet<QString>& visibleGroups, PasteOptions options)
{
    if (options.anchorPosition.has_value()) {
        return shiftedLabels(labels, *options.anchorPosition - labelGroupCenter(labels));
    }

    if (options.behavior == PasteBehavior::PreservePositionOnPaste) {
        return shiftedLabels(labels, {});
    }

    QPointF requestedShift(pastedLabelOffset, pastedLabelOffset);
    QVector<labelqt::core::Label> adjustedLabels = shiftedLabels(labels, requestedShift);
    if (!labelsCollideWithExistingLabels(adjustedLabels, image, visibleGroups)) {
        return adjustedLabels;
    }

    for (int attempt = 1; attempt <= 8; ++attempt) {
        requestedShift = QPointF(pastedLabelOffset * attempt, pastedLabelOffset * attempt);
        adjustedLabels = shiftedLabels(labels, requestedShift);
        if (!labelsCollideWithExistingLabels(adjustedLabels, image, visibleGroups)) {
            return adjustedLabels;
        }
    }

    return adjustedLabels;
}

} // namespace labelqt::services
