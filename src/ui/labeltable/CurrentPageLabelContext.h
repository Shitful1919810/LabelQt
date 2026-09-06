#pragma once

#include "core/Label.h"

#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>

class CurrentPageLabelContext final {
public:
    using LabelsProvider = std::function<const QVector<labelqt::core::Label>*()>;
    void setLabelsProvider(LabelsProvider provider);
    void setGroupFilter(QStringList groupFilter);
    void refresh();

    int visibleRowCount() const noexcept;
    int sourceIndexForRow(int row) const;
    int rowForSourceIndex(int sourceIndex) const;
    const labelqt::core::Label* labelForVisibleRow(int row) const;
    bool isSourceIndexVisible(int sourceIndex) const;
    QVector<int> visibleSourceIndexes(QVector<int> sourceIndexes) const;
    QVector<int> sourceIndexesInVisibleRowRange(int firstRow, int lastRow) const;

private:
    void rebuildVisibleRows();

    LabelsProvider m_labelsProvider;
    QVector<int> m_visibleRows;
    QSet<QString> m_groupFilter;
};
