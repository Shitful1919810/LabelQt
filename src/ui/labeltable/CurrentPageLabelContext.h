#pragma once

#include "core/Label.h"

#include <QSet>
#include <QStringList>
#include <QVector>

class CurrentPageLabelContext final {
public:
    void setLabels(QVector<labelqt::core::Label>* labels);
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

    QVector<labelqt::core::Label>* m_labels{nullptr};
    QVector<int> m_visibleRows;
    QSet<QString> m_groupFilter;
};
