#pragma once

#include "core/AppPreferences.h"
#include "core/Label.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QSet>
#include <QStringList>
#include <QVector>

class QMimeData;

class LabelTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        NumberColumn = 0,
        TextColumn = 1,
        GroupColumn = 2,
        ColumnCount = 3,
    };

    explicit LabelTableModel(QObject* parent = nullptr);

    void setLabels(QVector<labelqt::core::Label>* labels);
    void setGroupFilter(QStringList groupFilter);
    void setGroups(QStringList groups, QVector<labelqt::core::LabelGroupStyle> groupStyles);
    void refresh();
    void labelChanged(int row);
    int sourceIndexForRow(int row) const;
    int rowForSourceIndex(int sourceIndex) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    void labelEditRequested(int sourceIndex, int column, QVariant newValue);
    void labelsReorderRequested(QVector<int> sourceIndexes, int visibleDropRow);

private:
    void rebuildVisibleRows();
    QColor colorForGroup(const QString& group) const;

    QVector<labelqt::core::Label>* m_labels{nullptr};
    QVector<int> m_visibleRows;
    QSet<QString> m_groupFilter;
    QStringList m_groups;
    QVector<labelqt::core::LabelGroupStyle> m_groupStyles;
};
