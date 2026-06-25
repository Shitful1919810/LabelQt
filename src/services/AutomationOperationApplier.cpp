#include "services/AutomationOperationApplier.h"

#include <algorithm>

namespace labelqt::services {

namespace {
int imageIndexForPage(const labelqt::core::Project& project, const QString& page)
{
    const auto imageIt =
        std::find_if(project.images().cbegin(), project.images().cend(),
                     [&page](const labelqt::core::ImageEntry& image) { return image.name == page; });
    if (imageIt == project.images().cend()) {
        return -1;
    }
    return static_cast<int>(std::distance(project.images().cbegin(), imageIt));
}

bool isExistingEditableLabel(const labelqt::core::Project& project, int imageIndex, int labelIndex)
{
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return false;
    }
    const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
    return labelIndex >= 0 && labelIndex < image.labels.size() && !image.labels.at(labelIndex).isDeleted();
}
} // namespace

bool AutomationOperationApplyPlan::hasChanges() const noexcept
{
    return !labelGroupChanges.isEmpty() || !labelAdditions.isEmpty() || !labelTextChanges.isEmpty() ||
           !labelPositionChanges.isEmpty() || !labelDeletedChanges.isEmpty();
}

int AutomationOperationApplyPlan::changeCount() const noexcept
{
    return static_cast<int>(labelGroupChanges.size() + labelAdditions.size() + labelTextChanges.size() +
                            labelPositionChanges.size() + labelDeletedChanges.size());
}

AutomationOperationApplyPlan AutomationOperationApplier::plan(const labelqt::core::Project& project,
                                                              const QVector<AutomationOperation>& operations)
{
    AutomationOperationApplyPlan result;
    result.labelGroupChanges.reserve(operations.size());
    result.labelAdditions.reserve(operations.size());

    QVector<int> nextLabelIndexes;
    nextLabelIndexes.reserve(project.images().size());
    for (const labelqt::core::ImageEntry& image : project.images()) {
        nextLabelIndexes.append(static_cast<int>(image.labels.size()));
    }

    for (const AutomationOperation& operation : operations) {
        if (operation.type == QStringLiteral("addLabel")) {
            if (!project.groups().contains(operation.group)) {
                ++result.ignoredOperationCount;
                continue;
            }

            const int imageIndex = imageIndexForPage(project, operation.page);
            if (imageIndex < 0) {
                ++result.ignoredOperationCount;
                continue;
            }

            result.labelAdditions.append(
                {imageIndex, nextLabelIndexes[imageIndex],
                 labelqt::core::Label(operation.text, operation.group, QPointF(operation.x, operation.y))});
            ++nextLabelIndexes[imageIndex];
            continue;
        }

        const int imageIndex = imageIndexForPage(project, operation.page);
        if (!isExistingEditableLabel(project, imageIndex, operation.labelIndex)) {
            ++result.ignoredOperationCount;
            continue;
        }
        const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
        const labelqt::core::Label& label = image.labels.at(operation.labelIndex);

        if (operation.type == QStringLiteral("setLabelGroup")) {
            if (!project.groups().contains(operation.group)) {
                ++result.ignoredOperationCount;
                continue;
            }
            if (label.group() != operation.group) {
                result.labelGroupChanges.append({imageIndex, operation.labelIndex, label.group(), operation.group});
            }
            continue;
        }

        if (operation.type == QStringLiteral("setLabelText")) {
            if (label.text() != operation.text) {
                result.labelTextChanges.append({imageIndex, operation.labelIndex, label.text(), operation.text});
            }
            continue;
        }

        if (operation.type == QStringLiteral("setLabelPosition")) {
            const QPointF newPosition(operation.x, operation.y);
            if (label.position() != labelqt::core::Label(label.text(), label.group(), newPosition).position()) {
                result.labelPositionChanges.append(
                    {imageIndex, operation.labelIndex, label.position(), QPointF(operation.x, operation.y)});
            }
            continue;
        }

        if (operation.type == QStringLiteral("deleteLabel")) {
            result.labelDeletedChanges.append({imageIndex, operation.labelIndex, label.isDeleted(), true});
            continue;
        }

        ++result.ignoredOperationCount;
    }

    return result;
}

void AutomationOperationApplier::apply(labelqt::core::Project& project, const AutomationOperationApplyPlan& plan,
                                       bool redo)
{
    if (redo) {
        for (const AutomationLabelAddition& addition : plan.labelAdditions) {
            if (addition.imageIndex < 0 || addition.imageIndex >= project.images().size()) {
                continue;
            }
            labelqt::core::ImageEntry& image = project.images()[addition.imageIndex];
            if (addition.labelIndex < 0 || addition.labelIndex > image.labels.size()) {
                continue;
            }
            image.labels.insert(addition.labelIndex, addition.label);
        }
    }
    else {
        for (auto it = plan.labelAdditions.crbegin(); it != plan.labelAdditions.crend(); ++it) {
            const AutomationLabelAddition& addition = *it;
            if (addition.imageIndex < 0 || addition.imageIndex >= project.images().size()) {
                continue;
            }
            labelqt::core::ImageEntry& image = project.images()[addition.imageIndex];
            if (addition.labelIndex < 0 || addition.labelIndex >= image.labels.size()) {
                continue;
            }
            image.labels.removeAt(addition.labelIndex);
        }
    }

    for (const AutomationLabelGroupChange& change : plan.labelGroupChanges) {
        if (change.imageIndex < 0 || change.imageIndex >= project.images().size()) {
            continue;
        }
        labelqt::core::ImageEntry& image = project.images()[change.imageIndex];
        if (change.labelIndex < 0 || change.labelIndex >= image.labels.size()) {
            continue;
        }
        image.labels[change.labelIndex].setGroup(redo ? change.newGroup : change.oldGroup);
    }

    for (const AutomationLabelTextChange& change : plan.labelTextChanges) {
        if (change.imageIndex < 0 || change.imageIndex >= project.images().size()) {
            continue;
        }
        labelqt::core::ImageEntry& image = project.images()[change.imageIndex];
        if (change.labelIndex < 0 || change.labelIndex >= image.labels.size()) {
            continue;
        }
        image.labels[change.labelIndex].setText(redo ? change.newText : change.oldText);
    }

    for (const AutomationLabelPositionChange& change : plan.labelPositionChanges) {
        if (change.imageIndex < 0 || change.imageIndex >= project.images().size()) {
            continue;
        }
        labelqt::core::ImageEntry& image = project.images()[change.imageIndex];
        if (change.labelIndex < 0 || change.labelIndex >= image.labels.size()) {
            continue;
        }
        image.labels[change.labelIndex].setPosition(redo ? change.newPosition : change.oldPosition);
    }

    for (const AutomationLabelDeletedChange& change : plan.labelDeletedChanges) {
        if (change.imageIndex < 0 || change.imageIndex >= project.images().size()) {
            continue;
        }
        labelqt::core::ImageEntry& image = project.images()[change.imageIndex];
        if (change.labelIndex < 0 || change.labelIndex >= image.labels.size()) {
            continue;
        }
        image.labels[change.labelIndex].setDeleted(redo ? change.newDeleted : change.oldDeleted);
    }
}

} // namespace labelqt::services
