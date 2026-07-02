#pragma once

#include <QVector>

#include <functional>

class ImageCanvas;
class LabelTableModel;
class QTableView;

class LabelSelectionController final {
public:
    enum class SelectionOrigin {
        TableUser,
        Programmatic,
        CanvasHover,
    };

    struct SelectionOptions {
        SelectionOrigin origin{SelectionOrigin::Programmatic};
        bool focusTable{true};
    };

    using UpdateDetailsCallback = std::function<bool(int labelIndex)>;
    using SimpleCallback = std::function<void()>;
    using RowCallback = std::function<void(int row)>;

    void setWidgets(LabelTableModel* model, QTableView* tableView, ImageCanvas* canvas);
    void setCallbacks(UpdateDetailsCallback updateDetails, SimpleCallback clearDetails, RowCallback capRowHeight,
                      SimpleCallback focusTable);

    QVector<int> selectedLabelIndexes() const;
    bool isTableUserSelectionChange() const noexcept;
    bool selectSingle(int sourceIndex);
    bool selectSingle(int sourceIndex, SelectionOptions options);
    bool selectIndexes(QVector<int> sourceIndexes, int primarySourceIndex);
    bool selectIndexes(QVector<int> sourceIndexes, int primarySourceIndex, SelectionOptions options);
    void clearSelection(SelectionOrigin origin = SelectionOrigin::Programmatic);

private:
    void selectTableRows(const QVector<int>& sourceIndexes);

    LabelTableModel* m_model{nullptr};
    QTableView* m_tableView{nullptr};
    ImageCanvas* m_canvas{nullptr};
    UpdateDetailsCallback m_updateDetails;
    SimpleCallback m_clearDetails;
    RowCallback m_capRowHeight;
    SimpleCallback m_focusTable;
    SelectionOrigin m_activeSelectionOrigin{SelectionOrigin::TableUser};
};
