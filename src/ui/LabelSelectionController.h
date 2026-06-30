#pragma once

#include <QVector>

#include <functional>

class ImageCanvas;
class LabelTableModel;
class QTableView;

class LabelSelectionController final {
public:
    using UpdateDetailsCallback = std::function<bool(int labelIndex)>;
    using SimpleCallback = std::function<void()>;
    using RowCallback = std::function<void(int row)>;

    void setWidgets(LabelTableModel* model, QTableView* tableView, ImageCanvas* canvas);
    void setCallbacks(UpdateDetailsCallback updateDetails, SimpleCallback clearDetails, RowCallback capRowHeight,
                      SimpleCallback focusTable);

    QVector<int> selectedLabelIndexes() const;
    bool selectSingle(int sourceIndex);
    bool selectIndexes(QVector<int> sourceIndexes, int primarySourceIndex);
    void clearSelection();

private:
    void selectTableRows(const QVector<int>& sourceIndexes);

    LabelTableModel* m_model{nullptr};
    QTableView* m_tableView{nullptr};
    ImageCanvas* m_canvas{nullptr};
    UpdateDetailsCallback m_updateDetails;
    SimpleCallback m_clearDetails;
    RowCallback m_capRowHeight;
    SimpleCallback m_focusTable;
};
