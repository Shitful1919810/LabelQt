#include "services/label/LabelNavigator.h"
#include "services/label/LabelVisibility.h"

#include "core/Label.h"

#include <QVector>

namespace labelqt::services {
namespace {
QVector<int> visibleLabelIndexes(const labelqt::core::ImageEntry& image, const QStringList& visibleGroups)
{
    return LabelVisibility::sourceIndexes(image.labels, {visibleGroups.cbegin(), visibleGroups.cend()});
}

LabelNavigationTarget firstVisibleLabelInImage(const labelqt::core::Project& project, int imageIndex,
                                               const QStringList& visibleGroups)
{
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return {};
    }

    const QVector<int> indexes = visibleLabelIndexes(project.images().at(imageIndex), visibleGroups);
    return indexes.isEmpty() ? LabelNavigationTarget{} : LabelNavigationTarget{imageIndex, indexes.first()};
}

LabelNavigationTarget lastVisibleLabelInImage(const labelqt::core::Project& project, int imageIndex,
                                              const QStringList& visibleGroups)
{
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return {};
    }

    const QVector<int> indexes = visibleLabelIndexes(project.images().at(imageIndex), visibleGroups);
    return indexes.isEmpty() ? LabelNavigationTarget{} : LabelNavigationTarget{imageIndex, indexes.last()};
}
} // namespace

bool LabelNavigationTarget::isValid() const noexcept
{
    return imageIndex >= 0 && labelIndex >= 0;
}

LabelNavigationTarget LabelNavigator::nextVisibleLabel(const labelqt::core::Project& project,
                                                       const LabelNavigationState& state)
{
    if (project.isEmpty() || state.imageIndex < 0 || state.imageIndex >= project.images().size()) {
        return {};
    }

    const QVector<int> currentPageIndexes =
        visibleLabelIndexes(project.images().at(state.imageIndex), state.visibleGroups);
    const qsizetype currentIndexInVisibleList = currentPageIndexes.indexOf(state.labelIndex);
    if (currentIndexInVisibleList < 0) {
        return currentPageIndexes.isEmpty() ? LabelNavigationTarget{}
                                            : LabelNavigationTarget{state.imageIndex, currentPageIndexes.first()};
    }
    if (currentIndexInVisibleList + 1 < currentPageIndexes.size()) {
        return {state.imageIndex, currentPageIndexes.at(currentIndexInVisibleList + 1)};
    }

    const int imageCount = static_cast<int>(project.images().size());
    for (int imageIndex = state.imageIndex + 1; imageIndex < imageCount; ++imageIndex) {
        const LabelNavigationTarget target = firstVisibleLabelInImage(project, imageIndex, state.visibleGroups);
        if (target.isValid()) {
            return target;
        }
    }
    return {};
}

LabelNavigationTarget LabelNavigator::previousVisibleLabel(const labelqt::core::Project& project,
                                                           const LabelNavigationState& state)
{
    if (project.isEmpty() || state.imageIndex < 0 || state.imageIndex >= project.images().size()) {
        return {};
    }

    const QVector<int> currentPageIndexes =
        visibleLabelIndexes(project.images().at(state.imageIndex), state.visibleGroups);
    const qsizetype currentIndexInVisibleList = currentPageIndexes.indexOf(state.labelIndex);
    if (currentIndexInVisibleList < 0) {
        return currentPageIndexes.isEmpty() ? LabelNavigationTarget{}
                                            : LabelNavigationTarget{state.imageIndex, currentPageIndexes.last()};
    }
    if (currentIndexInVisibleList > 0) {
        return {state.imageIndex, currentPageIndexes.at(currentIndexInVisibleList - 1)};
    }

    for (int imageIndex = state.imageIndex - 1; imageIndex >= 0; --imageIndex) {
        const LabelNavigationTarget target = lastVisibleLabelInImage(project, imageIndex, state.visibleGroups);
        if (target.isValid()) {
            return target;
        }
    }
    return {};
}

} // namespace labelqt::services
