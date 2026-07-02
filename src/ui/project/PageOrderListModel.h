#pragma once

#include "core/Project.h"

#include <QAbstractTableModel>
#include <QVector>

class PageOrderListModel final : public QAbstractTableModel {
public:
    enum Column {
        ImageNameColumn = 0,
        OriginalIndexColumn,
        ColumnCount,
    };

    explicit PageOrderListModel(const labelqt::core::Project& project, QObject* parent = nullptr);

    QVector<int> pageOrder() const;
    QVector<int> lastMovedSourceIndexes() const;
    int sourceIndexForRow(int row) const;
    int rowForSourceIndex(int sourceIndex) const;
    void moveSourceIndexes(QVector<int> sourceIndexes, int delta);
    void removeSourceIndexes(QVector<int> sourceIndexes);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

private:
    void resetOrder(QVector<int> order, QVector<int> selectedSourceIndexes);

    const labelqt::core::Project& m_project;
    QVector<int> m_order;
    QVector<int> m_lastMovedSourceIndexes;
};
