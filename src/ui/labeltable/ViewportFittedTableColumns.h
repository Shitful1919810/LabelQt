#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>

class QEvent;
class QAbstractItemModel;
class QTableView;
class QWidget;

class ViewportFittedTableColumns final : public QObject {
    Q_OBJECT

public:
    struct Column {
        int logicalIndex{-1};
        int defaultWidth{80};
        int minimumWidth{40};
        bool stretch{false};
    };

    explicit ViewportFittedTableColumns(QTableView* tableView, QVector<Column> columns, QObject* parent = nullptr);
    ~ViewportFittedTableColumns() override;

    void fitToViewport();
    void rebalanceFromSectionResize(int logicalIndex, int oldSize, int newSize);
    void setColumnsChangedCallback(std::function<void()> callback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void attachModel(QAbstractItemModel* model);
    void scheduleFitToViewport();
    int columnConfigIndex(int logicalIndex) const;
    int minimumWidthExcept(int excludedConfigIndex) const;
    int currentColumnWidth(const Column& column) const;
    int defaultWidthSum() const;

    QTableView* m_tableView{nullptr};
    QPointer<QWidget> m_viewport;
    QPointer<QAbstractItemModel> m_model;
    QVector<Column> m_columns;
    std::function<void()> m_columnsChangedCallback;
    bool m_fitScheduled{false};
    bool m_isBalancing{false};
};
