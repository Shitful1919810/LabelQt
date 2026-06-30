#include "ui/LabelSelectionController.h"

#include "ui/ImageCanvas.h"
#include "ui/LabelTableModel.h"

#include <QAbstractItemView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QTableView>

#include <algorithm>
#include <utility>

void LabelSelectionController::setWidgets(LabelTableModel* model, QTableView* tableView, ImageCanvas* canvas)
{
    m_model = model;
    m_tableView = tableView;
    m_canvas = canvas;
}

void LabelSelectionController::setCallbacks(UpdateDetailsCallback updateDetails, SimpleCallback clearDetails,
                                            RowCallback capRowHeight, SimpleCallback focusTable)
{
    m_updateDetails = std::move(updateDetails);
    m_clearDetails = std::move(clearDetails);
    m_capRowHeight = std::move(capRowHeight);
    m_focusTable = std::move(focusTable);
}

QVector<int> LabelSelectionController::selectedLabelIndexes() const
{
    QVector<int> labelIndexes;
    if (m_tableView == nullptr || m_model == nullptr || m_tableView->selectionModel() == nullptr) {
        return labelIndexes;
    }

    const QModelIndexList rows = m_tableView->selectionModel()->selectedRows();
    labelIndexes.reserve(rows.size());
    for (const QModelIndex& row : rows) {
        const int sourceIndex = m_model->sourceIndexForRow(row.row());
        if (sourceIndex >= 0) {
            labelIndexes.append(sourceIndex);
        }
    }

    std::sort(labelIndexes.begin(), labelIndexes.end());
    labelIndexes.erase(std::unique(labelIndexes.begin(), labelIndexes.end()), labelIndexes.end());
    return labelIndexes;
}

bool LabelSelectionController::selectSingle(int sourceIndex)
{
    if (m_model == nullptr || m_tableView == nullptr || m_canvas == nullptr || !m_updateDetails ||
        !m_updateDetails(sourceIndex)) {
        clearSelection();
        return false;
    }

    const int visibleRow = m_model->rowForSourceIndex(sourceIndex);
    if (visibleRow >= 0) {
        const QModelIndex rowIndex = m_model->index(visibleRow, LabelTableModel::NumberColumn);
        if (m_tableView->selectionModel() != nullptr) {
            m_tableView->selectionModel()->select(rowIndex,
                                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_tableView->selectionModel()->setCurrentIndex(rowIndex,
                                                           QItemSelectionModel::Current | QItemSelectionModel::Rows);
        }
        m_tableView->scrollTo(rowIndex, QAbstractItemView::EnsureVisible);
        m_tableView->resizeRowToContents(visibleRow);
        if (m_capRowHeight) {
            m_capRowHeight(visibleRow);
        }
        if (m_focusTable) {
            m_focusTable();
        }
    }
    else {
        m_tableView->clearSelection();
    }
    m_canvas->setSelectedLabels({sourceIndex});
    return true;
}

bool LabelSelectionController::selectIndexes(QVector<int> sourceIndexes, int primarySourceIndex)
{
    if (m_model == nullptr || m_tableView == nullptr || m_tableView->selectionModel() == nullptr) {
        return false;
    }

    const QVector<int> visibleSourceIndexes = m_model->labelContext().visibleSourceIndexes(std::move(sourceIndexes));

    m_tableView->clearSelection();
    if (visibleSourceIndexes.isEmpty()) {
        clearSelection();
        return false;
    }

    if (!visibleSourceIndexes.contains(primarySourceIndex)) {
        primarySourceIndex = visibleSourceIndexes.last();
    }
    if (m_updateDetails) {
        m_updateDetails(primarySourceIndex);
    }
    if (m_canvas != nullptr) {
        m_canvas->setSelectedLabels(visibleSourceIndexes);
    }

    selectTableRows(visibleSourceIndexes);
    const int primaryVisibleRow = m_model->rowForSourceIndex(primarySourceIndex);
    if (primaryVisibleRow >= 0) {
        const QModelIndex primaryIndex = m_model->index(primaryVisibleRow, LabelTableModel::NumberColumn);
        m_tableView->selectionModel()->setCurrentIndex(primaryIndex,
                                                       QItemSelectionModel::Current | QItemSelectionModel::Rows);
        m_tableView->scrollTo(primaryIndex, QAbstractItemView::EnsureVisible);
        if (m_focusTable) {
            m_focusTable();
        }
    }
    m_tableView->viewport()->update();
    return true;
}

void LabelSelectionController::clearSelection()
{
    if (m_canvas != nullptr) {
        m_canvas->setSelectedLabels({});
    }
    if (m_tableView != nullptr) {
        m_tableView->clearSelection();
    }
    if (m_clearDetails) {
        m_clearDetails();
    }
}

void LabelSelectionController::selectTableRows(const QVector<int>& sourceIndexes)
{
    if (m_model == nullptr || m_tableView == nullptr || m_tableView->selectionModel() == nullptr) {
        return;
    }

    QItemSelection selection;
    for (int sourceIndex : sourceIndexes) {
        const int visibleRow = m_model->rowForSourceIndex(sourceIndex);
        if (visibleRow < 0) {
            continue;
        }
        selection.select(m_model->index(visibleRow, LabelTableModel::NumberColumn),
                         m_model->index(visibleRow, m_model->columnCount() - 1));
        m_tableView->resizeRowToContents(visibleRow);
        if (m_capRowHeight) {
            m_capRowHeight(visibleRow);
        }
    }

    m_tableView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
