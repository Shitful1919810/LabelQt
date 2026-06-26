#include "services/LabelEditController.h"

#include <algorithm>
#include <utility>

namespace labelqt::services {

namespace {
QString imageName(const labelqt::core::Project& project, int imageIndex)
{
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return {};
    }

    const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
    return image.name.isEmpty() ? image.path : image.name;
}

QString labelNumber(int labelIndex)
{
    return QString::number(labelIndex + 1).rightJustified(3, QLatin1Char('0'));
}

QString labelMessage(const QString& messageTemplate, const QString& fallback, const labelqt::core::Project& project,
                     int imageIndex, int labelIndex, const QString& group)
{
    if (messageTemplate.isEmpty()) {
        return fallback;
    }
    return QString(messageTemplate).arg(imageName(project, imageIndex), group, labelNumber(labelIndex));
}

bool labelEquals(const labelqt::core::Label& lhs, const labelqt::core::Label& rhs)
{
    return lhs.text() == rhs.text() && lhs.group() == rhs.group() && lhs.position() == rhs.position() &&
           lhs.isDeleted() == rhs.isDeleted();
}

bool labelVectorsEqual(const QVector<labelqt::core::Label>& lhs, const QVector<labelqt::core::Label>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        if (!labelEquals(lhs.at(i), rhs.at(i))) {
            return false;
        }
    }
    return true;
}
} // namespace

LabelEditController::LabelEditController(labelqt::core::Project& project, labelqt::core::UndoStack& undoStack,
                                         LabelEditCommandTexts commandTexts)
    : m_project(project), m_undoStack(undoStack), m_commandTexts(std::move(commandTexts))
{
}

void LabelEditController::setCallbacks(LabelSelectedCallback labelSelected, LabelsSelectedCallback labelsSelected,
                                       ImageSelectionClearedCallback imageSelectionCleared,
                                       ProjectChangedCallback projectChanged, DirtyCallback dirty)
{
    m_labelSelected = std::move(labelSelected);
    m_labelsSelected = std::move(labelsSelected);
    m_imageSelectionCleared = std::move(imageSelectionCleared);
    m_projectChanged = std::move(projectChanged);
    m_dirty = std::move(dirty);
}

LabelEditResult LabelEditController::addGroup(const QString& group)
{
    const QString trimmedGroup = group.trimmed();
    if (trimmedGroup.isEmpty() || m_project.groups().contains(trimmedGroup)) {
        return {};
    }

    const QStringList oldGroups = m_project.groups();
    const QVector<QVector<QString>> oldLabelGroups = currentLabelGroups();
    QStringList newGroups = oldGroups;
    newGroups.append(trimmedGroup);

    m_project.setGroups(newGroups);
    const QString message = m_commandTexts.addGroupMessage.isEmpty()
                                ? m_commandTexts.addGroup
                                : QString(m_commandTexts.addGroupMessage).arg(trimmedGroup);
    m_undoStack.push(
        m_commandTexts.addGroup,
        message,
        message,
        [this, oldGroups, oldLabelGroups]() { applyGroupsAndLabelGroups(oldGroups, oldLabelGroups); },
        [this, newGroups, oldLabelGroups]() { applyGroupsAndLabelGroups(newGroups, oldLabelGroups); });
    if (m_projectChanged) {
        m_projectChanged();
    }
    markDirty();
    LabelEditResult result;
    result.changed = true;
    return result;
}

LabelEditResult LabelEditController::removeGroup(const QString& group, const QString& fallbackGroup)
{
    if (group.isEmpty() || group == fallbackGroup || !m_project.groups().contains(group) ||
        !m_project.groups().contains(fallbackGroup) || m_project.groups().size() <= 1) {
        return {};
    }

    const QStringList oldGroups = m_project.groups();
    const QVector<QVector<QString>> oldLabelGroups = currentLabelGroups();
    QStringList newGroups = oldGroups;
    newGroups.removeAll(group);

    for (labelqt::core::ImageEntry& image : m_project.images()) {
        for (labelqt::core::Label& label : image.labels) {
            if (label.group() == group) {
                label.setGroup(fallbackGroup);
            }
        }
    }
    const QVector<QVector<QString>> newLabelGroups = currentLabelGroups();
    m_project.setGroups(newGroups);

    const QString message = m_commandTexts.removeGroupMessage.isEmpty()
                                ? m_commandTexts.removeGroup
                                : QString(m_commandTexts.removeGroupMessage).arg(group);
    m_undoStack.push(
        m_commandTexts.removeGroup,
        message,
        message,
        [this, oldGroups, oldLabelGroups]() { applyGroupsAndLabelGroups(oldGroups, oldLabelGroups); },
        [this, newGroups, newLabelGroups]() { applyGroupsAndLabelGroups(newGroups, newLabelGroups); });
    if (m_projectChanged) {
        m_projectChanged();
    }
    markDirty();
    LabelEditResult result;
    result.changed = true;
    return result;
}

LabelEditResult LabelEditController::addLabel(int imageIndex, const labelqt::core::Label& label)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr) {
        return {};
    }

    image->labels.append(label);
    const int labelIndex = static_cast<int>(image->labels.size()) - 1;
    const labelqt::core::Label addedLabel = image->labels.last();
    const QString message =
        labelMessage(m_commandTexts.addLabelMessage, m_commandTexts.addLabel, m_project, imageIndex, labelIndex,
                     addedLabel.group());
    m_undoStack.push(
        m_commandTexts.addLabel,
        message,
        message,
        [this, imageIndex, labelIndex, addedLabel]() {
            labelqt::core::ImageEntry* targetImage = imageAt(imageIndex);
            if (targetImage == nullptr || labelIndex < 0 || labelIndex >= targetImage->labels.size()) {
                return;
            }

            const labelqt::core::Label& currentLabel = targetImage->labels.at(labelIndex);
            if (!labelEquals(currentLabel, addedLabel)) {
                return;
            }

            targetImage->labels.removeAt(labelIndex);
            if (m_imageSelectionCleared) {
                m_imageSelectionCleared(imageIndex);
            }
            markDirty();
        },
        [this, imageIndex, labelIndex, addedLabel]() {
            labelqt::core::ImageEntry* targetImage = imageAt(imageIndex);
            if (targetImage == nullptr || labelIndex < 0 || labelIndex > targetImage->labels.size()) {
                return;
            }

            targetImage->labels.insert(labelIndex, addedLabel);
            if (m_labelSelected) {
                m_labelSelected(imageIndex, labelIndex);
            }
            markDirty();
        });

    markDirty();
    return {true, labelIndex, {}, {labelIndex}};
}

LabelEditResult LabelEditController::pasteLabels(int imageIndex, QVector<labelqt::core::Label> labels,
                                                 int insertAfterLabelIndex)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labels.isEmpty()) {
        return {};
    }

    labels.erase(std::remove_if(labels.begin(), labels.end(),
                                [this](const labelqt::core::Label& label) {
                                    return label.isDeleted() || !hasGroup(label.group());
                                }),
                 labels.end());
    if (labels.isEmpty()) {
        return {};
    }

    const QVector<labelqt::core::Label> oldLabels = image->labels;
    int insertIndex = static_cast<int>(image->labels.size());
    if (insertAfterLabelIndex >= 0 && insertAfterLabelIndex < image->labels.size()) {
        insertIndex = insertAfterLabelIndex + 1;
    }
    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(image->labels.size()));

    QVector<int> selectedIndexes;
    selectedIndexes.reserve(labels.size());
    for (int i = 0; i < labels.size(); ++i) {
        image->labels.insert(insertIndex + i, labels.at(i));
        selectedIndexes.append(insertIndex + i);
    }
    const QVector<labelqt::core::Label> newLabels = image->labels;

    const QString message =
        m_commandTexts.pasteLabelsMessage.isEmpty()
            ? m_commandTexts.pasteLabels
            : QString(m_commandTexts.pasteLabelsMessage)
                  .arg(imageName(m_project, imageIndex), QString::number(labels.size()));
    m_undoStack.push(
        m_commandTexts.pasteLabels,
        message,
        message,
        [this, imageIndex, oldLabels]() { applyLabelOrder(imageIndex, oldLabels, {}); },
        [this, imageIndex, newLabels, selectedIndexes]() {
            applyLabelOrder(imageIndex, newLabels, selectedIndexes);
        });
    markDirty();
    return {true, selectedIndexes.last(), selectedIndexes, selectedIndexes};
}

LabelEditResult LabelEditController::deleteLabels(int imageIndex, const QVector<int>& labelIndexes)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.isEmpty()) {
        return {};
    }

    QVector<int> changedIndexes;
    QVector<bool> oldDeleted;
    for (int labelIndex : labelIndexes) {
        if (labelIndex < 0 || labelIndex >= image->labels.size() || image->labels.at(labelIndex).isDeleted()) {
            continue;
        }
        changedIndexes.append(labelIndex);
        oldDeleted.append(false);
        image->labels[labelIndex].setDeleted(true);
    }

    if (changedIndexes.isEmpty()) {
        return {};
    }

    const QString message = changedIndexes.size() == 1
                                ? labelMessage(m_commandTexts.deleteLabelMessage, m_commandTexts.deleteLabels,
                                               m_project, imageIndex, changedIndexes.first(),
                                               image->labels.at(changedIndexes.first()).group())
                                : m_commandTexts.deleteLabels;
    m_undoStack.push(
        m_commandTexts.deleteLabels,
        message,
        message,
        [this, imageIndex, labelIndexes = changedIndexes, oldDeleted]() {
            applyBatchLabelDeleted(imageIndex, labelIndexes, oldDeleted);
        },
        [this, imageIndex, labelIndexes = changedIndexes]() {
            applyBatchLabelDeleted(imageIndex, labelIndexes, QVector<bool>(labelIndexes.size(), true));
        });
    markDirty();
    return {true, -1, {}, changedIndexes};
}

LabelEditResult LabelEditController::changeLabelsGroup(int imageIndex, const QVector<int>& labelIndexes,
                                                       const QString& group)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.isEmpty() || !hasGroup(group)) {
        return {};
    }

    QVector<int> changedIndexes;
    QVector<QString> oldGroups;
    QVector<QString> newGroups;
    for (int labelIndex : labelIndexes) {
        if (labelIndex < 0 || labelIndex >= image->labels.size() || image->labels.at(labelIndex).isDeleted() ||
            image->labels.at(labelIndex).group() == group) {
            continue;
        }

        changedIndexes.append(labelIndex);
        oldGroups.append(image->labels.at(labelIndex).group());
        newGroups.append(group);
        image->labels[labelIndex].setGroup(group);
    }

    if (changedIndexes.isEmpty()) {
        return {};
    }

    const QString message = changedIndexes.size() == 1
                                ? labelMessage(m_commandTexts.changeLabelGroupMessage,
                                               m_commandTexts.changeLabelGroup, m_project, imageIndex,
                                               changedIndexes.first(), group)
                                : m_commandTexts.changeLabelGroup;
    m_undoStack.push(
        m_commandTexts.changeLabelGroup,
        message,
        message,
        [this, imageIndex, labelIndexes = changedIndexes, oldGroups]() {
            applyBatchLabelGroups(imageIndex, labelIndexes, oldGroups);
        },
        [this, imageIndex, labelIndexes = changedIndexes, newGroups]() {
            applyBatchLabelGroups(imageIndex, labelIndexes, newGroups);
        });
    markDirty();
    return {true, changedIndexes.last(), {}, changedIndexes};
}

LabelEditResult LabelEditController::reorderLabels(int imageIndex, QVector<int> sourceIndexes,
                                                   int insertBeforeSourceIndex)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || sourceIndexes.isEmpty()) {
        return {};
    }

    std::sort(sourceIndexes.begin(), sourceIndexes.end());
    sourceIndexes.erase(std::unique(sourceIndexes.begin(), sourceIndexes.end()), sourceIndexes.end());
    sourceIndexes.erase(std::remove_if(sourceIndexes.begin(), sourceIndexes.end(),
                                       [image](int sourceIndex) {
                                           return sourceIndex < 0 || sourceIndex >= image->labels.size() ||
                                                  image->labels.at(sourceIndex).isDeleted();
                                       }),
                        sourceIndexes.end());
    if (sourceIndexes.isEmpty()) {
        return {};
    }

    insertBeforeSourceIndex = std::clamp(insertBeforeSourceIndex, 0, static_cast<int>(image->labels.size()));

    struct IndexedLabel {
        int oldIndex;
        labelqt::core::Label label;
    };

    const QVector<labelqt::core::Label> oldLabels = image->labels;
    QVector<IndexedLabel> movingLabels;
    QVector<IndexedLabel> remainingLabels;
    movingLabels.reserve(sourceIndexes.size());
    remainingLabels.reserve(oldLabels.size() - sourceIndexes.size());

    for (int i = 0; i < oldLabels.size(); ++i) {
        IndexedLabel indexedLabel{i, oldLabels.at(i)};
        if (std::binary_search(sourceIndexes.cbegin(), sourceIndexes.cend(), i)) {
            movingLabels.append(std::move(indexedLabel));
        }
        else {
            remainingLabels.append(std::move(indexedLabel));
        }
    }
    if (movingLabels.isEmpty()) {
        return {};
    }

    int remainingInsertIndex = 0;
    for (int i = 0; i < insertBeforeSourceIndex; ++i) {
        if (!std::binary_search(sourceIndexes.cbegin(), sourceIndexes.cend(), i)) {
            ++remainingInsertIndex;
        }
    }
    remainingInsertIndex = std::clamp(remainingInsertIndex, 0, static_cast<int>(remainingLabels.size()));

    QVector<IndexedLabel> reorderedLabels;
    reorderedLabels.reserve(oldLabels.size());
    for (int i = 0; i < remainingInsertIndex; ++i) {
        reorderedLabels.append(std::move(remainingLabels[i]));
    }
    for (IndexedLabel& label : movingLabels) {
        reorderedLabels.append(std::move(label));
    }
    for (int i = remainingInsertIndex; i < remainingLabels.size(); ++i) {
        reorderedLabels.append(std::move(remainingLabels[i]));
    }

    QVector<labelqt::core::Label> newLabels;
    QVector<int> newSelectedIndexes;
    newLabels.reserve(reorderedLabels.size());
    newSelectedIndexes.reserve(sourceIndexes.size());
    for (int i = 0; i < reorderedLabels.size(); ++i) {
        if (std::binary_search(sourceIndexes.cbegin(), sourceIndexes.cend(), reorderedLabels.at(i).oldIndex)) {
            newSelectedIndexes.append(i);
        }
        newLabels.append(reorderedLabels.at(i).label);
    }

    if (labelVectorsEqual(oldLabels, newLabels)) {
        return {};
    }

    image->labels = newLabels;
    const QString message =
        m_commandTexts.reorderLabelsMessage.isEmpty()
            ? m_commandTexts.reorderLabels
            : QString(m_commandTexts.reorderLabelsMessage).arg(imageName(m_project, imageIndex));
    m_undoStack.push(
        m_commandTexts.reorderLabels,
        message,
        message,
        [this, imageIndex, oldLabels, sourceIndexes]() { applyLabelOrder(imageIndex, oldLabels, sourceIndexes); },
        [this, imageIndex, newLabels, newSelectedIndexes]() {
            applyLabelOrder(imageIndex, newLabels, newSelectedIndexes);
        });
    markDirty();
    return {true, -1, newSelectedIndexes, newSelectedIndexes};
}

LabelEditResult LabelEditController::setLabelText(int imageIndex, int labelIndex, const QString& text,
                                                  bool registerUndo)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size()) {
        return {};
    }

    const QString oldText = image->labels.at(labelIndex).text();
    if (oldText == text) {
        return {};
    }

    if (registerUndo) {
        registerLabelTextUndo(imageIndex, labelIndex, oldText, text);
    }
    image->labels[labelIndex].setText(text);
    markDirty();
    return {true, labelIndex, {}, {labelIndex}};
}

LabelEditResult LabelEditController::setLabelGroup(int imageIndex, int labelIndex, const QString& group,
                                                   bool registerUndo)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size() || !hasGroup(group)) {
        return {};
    }

    const QString oldGroup = image->labels.at(labelIndex).group();
    if (oldGroup == group) {
        return {};
    }

    if (registerUndo) {
        registerLabelGroupUndo(imageIndex, labelIndex, oldGroup, group);
    }
    image->labels[labelIndex].setGroup(group);
    markDirty();
    return {true, labelIndex, {}, {labelIndex}};
}

LabelEditResult LabelEditController::setLabelPosition(int imageIndex, int labelIndex, QPointF normalizedPosition,
                                                      bool registerUndo)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size()) {
        return {};
    }

    const QPointF oldPosition = image->labels.at(labelIndex).position();
    image->labels[labelIndex].setPosition(normalizedPosition);
    const QPointF newPosition = image->labels.at(labelIndex).position();
    if (oldPosition == newPosition) {
        return {};
    }

    if (registerUndo) {
        const QString message =
            labelMessage(m_commandTexts.moveLabelMessage, m_commandTexts.moveLabel, m_project, imageIndex, labelIndex,
                         image->labels.at(labelIndex).group());
        m_undoStack.push(
            m_commandTexts.moveLabel,
            message,
            message,
            [this, imageIndex, labelIndex, oldPosition]() { applyLabelPosition(imageIndex, labelIndex, oldPosition); },
            [this, imageIndex, labelIndex, newPosition]() { applyLabelPosition(imageIndex, labelIndex, newPosition); });
    }
    markDirty();
    return {true, labelIndex, {}, {labelIndex}};
}

LabelEditResult LabelEditController::setLabelPositions(int imageIndex, QVector<int> labelIndexes,
                                                       QVector<QPointF> normalizedPositions, bool registerUndo)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.size() != normalizedPositions.size() || labelIndexes.isEmpty()) {
        return {};
    }

    QVector<int> changedIndexes;
    QVector<QPointF> oldPositions;
    QVector<QPointF> newPositions;
    changedIndexes.reserve(labelIndexes.size());
    oldPositions.reserve(labelIndexes.size());
    newPositions.reserve(labelIndexes.size());

    for (int i = 0; i < labelIndexes.size(); ++i) {
        const int labelIndex = labelIndexes.at(i);
        if (labelIndex < 0 || labelIndex >= image->labels.size()) {
            continue;
        }

        const QPointF oldPosition = image->labels.at(labelIndex).position();
        image->labels[labelIndex].setPosition(normalizedPositions.at(i));
        const QPointF newPosition = image->labels.at(labelIndex).position();
        if (oldPosition == newPosition) {
            continue;
        }

        changedIndexes.append(labelIndex);
        oldPositions.append(oldPosition);
        newPositions.append(newPosition);
    }

    if (changedIndexes.isEmpty()) {
        return {};
    }

    if (registerUndo) {
        const QString message =
            changedIndexes.size() == 1
                ? labelMessage(m_commandTexts.moveLabelMessage, m_commandTexts.moveLabel, m_project, imageIndex,
                               changedIndexes.first(), image->labels.at(changedIndexes.first()).group())
                : m_commandTexts.moveLabel;
        m_undoStack.push(
            m_commandTexts.moveLabel,
            message,
            message,
            [this, imageIndex, changedIndexes, oldPositions]() {
                applyBatchLabelPositions(imageIndex, changedIndexes, oldPositions);
            },
            [this, imageIndex, changedIndexes, newPositions]() {
                applyBatchLabelPositions(imageIndex, changedIndexes, newPositions);
            });
    }
    markDirty();
    return {true, changedIndexes.last(), changedIndexes, changedIndexes};
}

void LabelEditController::registerLabelTextUndo(int imageIndex, int labelIndex, const QString& oldText,
                                                const QString& newText)
{
    if (oldText == newText) {
        return;
    }

    const labelqt::core::ImageEntry* image = imageAt(imageIndex);
    const QString group =
        image != nullptr && labelIndex >= 0 && labelIndex < image->labels.size() ? image->labels.at(labelIndex).group()
                                                                                 : QString();
    const QString message = labelMessage(m_commandTexts.editLabelTextMessage, m_commandTexts.editLabelText, m_project,
                                         imageIndex, labelIndex, group);
    m_undoStack.push(
        m_commandTexts.editLabelText,
        message,
        message,
        [this, imageIndex, labelIndex, oldText]() { applyLabelText(imageIndex, labelIndex, oldText); },
        [this, imageIndex, labelIndex, newText]() { applyLabelText(imageIndex, labelIndex, newText); });
}

void LabelEditController::registerLabelGroupUndo(int imageIndex, int labelIndex, const QString& oldGroup,
                                                 const QString& newGroup)
{
    if (oldGroup == newGroup) {
        return;
    }

    const QString message = labelMessage(m_commandTexts.changeLabelGroupMessage, m_commandTexts.changeLabelGroup,
                                         m_project, imageIndex, labelIndex, newGroup);
    m_undoStack.push(
        m_commandTexts.changeLabelGroup,
        message,
        message,
        [this, imageIndex, labelIndex, oldGroup]() { applyLabelGroup(imageIndex, labelIndex, oldGroup); },
        [this, imageIndex, labelIndex, newGroup]() { applyLabelGroup(imageIndex, labelIndex, newGroup); });
}

void LabelEditController::applyLabelText(int imageIndex, int labelIndex, const QString& text)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size()) {
        return;
    }

    image->labels[labelIndex].setText(text);
    if (m_labelSelected) {
        m_labelSelected(imageIndex, labelIndex);
    }
    markDirty();
}

void LabelEditController::applyLabelGroup(int imageIndex, int labelIndex, const QString& group)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size() || !hasGroup(group)) {
        return;
    }

    image->labels[labelIndex].setGroup(group);
    if (m_labelSelected) {
        m_labelSelected(imageIndex, labelIndex);
    }
    markDirty();
}

void LabelEditController::applyLabelPosition(int imageIndex, int labelIndex, QPointF normalizedPosition)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndex < 0 || labelIndex >= image->labels.size()) {
        return;
    }

    image->labels[labelIndex].setPosition(normalizedPosition);
    if (m_labelSelected) {
        m_labelSelected(imageIndex, labelIndex);
    }
    markDirty();
}

void LabelEditController::applyBatchLabelPositions(int imageIndex, QVector<int> labelIndexes,
                                                   QVector<QPointF> normalizedPositions)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.size() != normalizedPositions.size()) {
        return;
    }

    QVector<int> validIndexes;
    validIndexes.reserve(labelIndexes.size());
    for (int i = 0; i < labelIndexes.size(); ++i) {
        const int labelIndex = labelIndexes.at(i);
        if (labelIndex < 0 || labelIndex >= image->labels.size()) {
            continue;
        }
        image->labels[labelIndex].setPosition(normalizedPositions.at(i));
        validIndexes.append(labelIndex);
    }

    if (!validIndexes.isEmpty() && m_labelsSelected) {
        m_labelsSelected(imageIndex, std::move(validIndexes));
    }
    else if (m_imageSelectionCleared) {
        m_imageSelectionCleared(imageIndex);
    }
    markDirty();
}

void LabelEditController::applyLabelOrder(int imageIndex, QVector<labelqt::core::Label> labels,
                                          QVector<int> selectedIndexes)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr) {
        return;
    }

    image->labels = std::move(labels);
    if (m_labelsSelected) {
        m_labelsSelected(imageIndex, std::move(selectedIndexes));
    }
    markDirty();
}

void LabelEditController::applyBatchLabelGroups(int imageIndex, QVector<int> labelIndexes, QVector<QString> groups)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.size() != groups.size()) {
        return;
    }

    int lastValidIndex = -1;
    for (int i = 0; i < labelIndexes.size(); ++i) {
        const int labelIndex = labelIndexes.at(i);
        if (labelIndex < 0 || labelIndex >= image->labels.size() || !hasGroup(groups.at(i))) {
            continue;
        }
        image->labels[labelIndex].setGroup(groups.at(i));
        lastValidIndex = labelIndex;
    }

    if (lastValidIndex >= 0 && m_labelSelected) {
        m_labelSelected(imageIndex, lastValidIndex);
    }
    else if (m_imageSelectionCleared) {
        m_imageSelectionCleared(imageIndex);
    }
    markDirty();
}

void LabelEditController::applyBatchLabelDeleted(int imageIndex, QVector<int> labelIndexes, QVector<bool> deleted)
{
    labelqt::core::ImageEntry* image = imageAt(imageIndex);
    if (image == nullptr || labelIndexes.size() != deleted.size()) {
        return;
    }

    int lastRestoredIndex = -1;
    for (int i = 0; i < labelIndexes.size(); ++i) {
        const int labelIndex = labelIndexes.at(i);
        if (labelIndex < 0 || labelIndex >= image->labels.size()) {
            continue;
        }
        image->labels[labelIndex].setDeleted(deleted.at(i));
        if (!deleted.at(i)) {
            lastRestoredIndex = labelIndex;
        }
    }

    if (lastRestoredIndex >= 0 && m_labelSelected) {
        m_labelSelected(imageIndex, lastRestoredIndex);
    }
    else if (m_imageSelectionCleared) {
        m_imageSelectionCleared(imageIndex);
    }
    markDirty();
}

void LabelEditController::applyGroupsAndLabelGroups(QStringList groups, QVector<QVector<QString>> labelGroups)
{
    m_project.setGroups(std::move(groups));
    for (int imageIndex = 0; imageIndex < m_project.images().size() && imageIndex < labelGroups.size(); ++imageIndex) {
        labelqt::core::ImageEntry& image = m_project.images()[imageIndex];
        const QVector<QString>& imageLabelGroups = labelGroups.at(imageIndex);
        for (int labelIndex = 0; labelIndex < image.labels.size() && labelIndex < imageLabelGroups.size();
             ++labelIndex) {
            image.labels[labelIndex].setGroup(imageLabelGroups.at(labelIndex));
        }
    }

    if (m_projectChanged) {
        m_projectChanged();
    }
    markDirty();
}

QVector<QVector<QString>> LabelEditController::currentLabelGroups() const
{
    QVector<QVector<QString>> labelGroups;
    labelGroups.reserve(m_project.images().size());
    for (const labelqt::core::ImageEntry& image : m_project.images()) {
        QVector<QString> imageLabelGroups;
        imageLabelGroups.reserve(image.labels.size());
        for (const labelqt::core::Label& label : image.labels) {
            imageLabelGroups.append(label.group());
        }
        labelGroups.append(std::move(imageLabelGroups));
    }
    return labelGroups;
}

labelqt::core::ImageEntry* LabelEditController::imageAt(int imageIndex)
{
    if (imageIndex < 0 || imageIndex >= m_project.images().size()) {
        return nullptr;
    }
    return &m_project.images()[imageIndex];
}

const labelqt::core::ImageEntry* LabelEditController::imageAt(int imageIndex) const
{
    if (imageIndex < 0 || imageIndex >= m_project.images().size()) {
        return nullptr;
    }
    return &m_project.images().at(imageIndex);
}

bool LabelEditController::hasGroup(const QString& group) const
{
    return m_project.groups().contains(group);
}

void LabelEditController::markDirty()
{
    if (m_dirty) {
        m_dirty();
    }
}

} // namespace labelqt::services
