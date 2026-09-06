#include "ui/labeltable/CurrentPageLabelContext.h"

#include <algorithm>
#include <utility>

void CurrentPageLabelContext::setLabelsProvider(LabelsProvider provider)
{
    m_labelsProvider = std::move(provider);
    rebuildVisibleRows();
}

void CurrentPageLabelContext::setGroupFilter(QStringList groupFilter)
{
    m_groupFilter = QSet<QString>(groupFilter.cbegin(), groupFilter.cend());
    rebuildVisibleRows();
}

void CurrentPageLabelContext::refresh()
{
    rebuildVisibleRows();
}

int CurrentPageLabelContext::visibleRowCount() const noexcept
{
    return static_cast<int>(m_visibleRows.size());
}

int CurrentPageLabelContext::sourceIndexForRow(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_visibleRows.size())) {
        return -1;
    }
    const int sourceIndex = m_visibleRows.at(row);
    const auto* labels = m_labelsProvider ? m_labelsProvider() : nullptr;
    if (labels == nullptr || sourceIndex < 0 || sourceIndex >= static_cast<int>(labels->size())) {
        return -1;
    }
    return sourceIndex;
}

int CurrentPageLabelContext::rowForSourceIndex(int sourceIndex) const
{
    const auto* labels = m_labelsProvider ? m_labelsProvider() : nullptr;
    if (labels == nullptr || sourceIndex < 0 || sourceIndex >= static_cast<int>(labels->size())) {
        return -1;
    }
    for (int row = 0; row < static_cast<int>(m_visibleRows.size()); ++row) {
        if (m_visibleRows.at(row) == sourceIndex) {
            return row;
        }
    }
    return -1;
}

const labelqt::core::Label* CurrentPageLabelContext::labelForVisibleRow(int row) const
{
    const int sourceIndex = sourceIndexForRow(row);
    const auto* labels = m_labelsProvider ? m_labelsProvider() : nullptr;
    if (sourceIndex < 0 || labels == nullptr || sourceIndex >= labels->size()) {
        return nullptr;
    }
    return &labels->at(sourceIndex);
}

bool CurrentPageLabelContext::isSourceIndexVisible(int sourceIndex) const
{
    return rowForSourceIndex(sourceIndex) >= 0;
}

QVector<int> CurrentPageLabelContext::visibleSourceIndexes(QVector<int> sourceIndexes) const
{
    sourceIndexes.erase(std::remove_if(sourceIndexes.begin(), sourceIndexes.end(),
                                       [this](int sourceIndex) { return !isSourceIndexVisible(sourceIndex); }),
                        sourceIndexes.end());
    std::sort(sourceIndexes.begin(), sourceIndexes.end());
    sourceIndexes.erase(std::unique(sourceIndexes.begin(), sourceIndexes.end()), sourceIndexes.end());
    return sourceIndexes;
}

QVector<int> CurrentPageLabelContext::sourceIndexesInVisibleRowRange(int firstRow, int lastRow) const
{
    if (visibleRowCount() <= 0) {
        return {};
    }

    if (firstRow > lastRow) {
        std::swap(firstRow, lastRow);
    }

    firstRow = std::clamp(firstRow, 0, visibleRowCount() - 1);
    lastRow = std::clamp(lastRow, 0, visibleRowCount() - 1);
    if (firstRow > lastRow) {
        return {};
    }

    QVector<int> sourceIndexes;
    sourceIndexes.reserve(lastRow - firstRow + 1);
    for (int row = firstRow; row <= lastRow; ++row) {
        const int sourceIndex = sourceIndexForRow(row);
        if (sourceIndex >= 0) {
            sourceIndexes.append(sourceIndex);
        }
    }
    return sourceIndexes;
}

void CurrentPageLabelContext::rebuildVisibleRows()
{
    m_visibleRows.clear();
    const auto* labels = m_labelsProvider ? m_labelsProvider() : nullptr;
    if (labels == nullptr) {
        return;
    }

    for (int i = 0; i < static_cast<int>(labels->size()); ++i) {
        const labelqt::core::Label& label = labels->at(i);
        if (!label.isDeleted() && m_groupFilter.contains(label.group())) {
            m_visibleRows.append(i);
        }
    }
}
