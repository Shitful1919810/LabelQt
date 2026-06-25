#include "ui/ViewportFittedTableColumns.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr int narrowViewportMinimumWidth = 24;
}

ViewportFittedTableColumns::ViewportFittedTableColumns(QTableView* tableView, QVector<Column> columns, QObject* parent)
    : QObject(parent), m_tableView(tableView), m_columns(std::move(columns))
{
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr || m_columns.isEmpty()) {
        return;
    }

    QHeaderView* header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionsMovable(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(m_columns.last().logicalIndex, QHeaderView::Fixed);
    header->setMinimumSectionSize(narrowViewportMinimumWidth);
    m_tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tableView->installEventFilter(this);
    m_tableView->viewport()->installEventFilter(this);
    attachModel(m_tableView->model());
    scheduleFitToViewport();
}

void ViewportFittedTableColumns::fitToViewport()
{
    if (m_isBalancing || m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr || m_columns.isEmpty()) {
        return;
    }

    const int viewportWidth = m_tableView->viewport()->width();
    if (viewportWidth <= 0) {
        return;
    }

    QHeaderView* header = m_tableView->horizontalHeader();
    QVector<int> widths;
    widths.reserve(m_columns.size());
    for (const Column& column : std::as_const(m_columns)) {
        widths.append(currentColumnWidth(column));
    }

    const int stretchIndex = stretchColumnConfigIndex();
    const int minimumTotal = minimumWidthExcept(-1);
    if (viewportWidth >= minimumTotal && stretchIndex >= 0) {
        int remainingWidth = viewportWidth;
        for (int i = 0; i < m_columns.size(); ++i) {
            if (i == stretchIndex) {
                continue;
            }

            const int maximumWidth = remainingWidth - minimumWidthExcept(i) + m_columns.at(i).minimumWidth;
            widths[i] = std::clamp(widths.at(i), m_columns.at(i).minimumWidth, maximumWidth);
            remainingWidth -= widths.at(i);
        }
        widths[stretchIndex] = std::max(m_columns.at(stretchIndex).minimumWidth, remainingWidth);
    }
    else {
        const int defaultsTotal = std::max(1, defaultWidthSum());
        int assignedWidth = 0;
        for (int i = 0; i < m_columns.size(); ++i) {
            widths[i] =
                std::max(narrowViewportMinimumWidth, viewportWidth * m_columns.at(i).defaultWidth / defaultsTotal);
            assignedWidth += widths.at(i);
        }
        if (stretchIndex >= 0) {
            widths[stretchIndex] =
                std::max(narrowViewportMinimumWidth, widths.at(stretchIndex) + viewportWidth - assignedWidth);
        }
    }

    const QSignalBlocker blocker(header);
    m_isBalancing = true;
    for (int i = 0; i < m_columns.size(); ++i) {
        header->resizeSection(m_columns.at(i).logicalIndex, widths.at(i));
    }
    m_isBalancing = false;

    if (m_columnsChangedCallback) {
        m_columnsChangedCallback();
    }
}

void ViewportFittedTableColumns::rebalanceFromSectionResize(int logicalIndex, int oldSize, int newSize)
{
    const int configIndex = columnConfigIndex(logicalIndex);
    if (m_isBalancing || m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr || configIndex < 0 ||
        configIndex >= m_columns.size() - 1 || oldSize == newSize) {
        return;
    }

    QHeaderView* header = m_tableView->horizontalHeader();
    const Column& currentColumn = m_columns.at(configIndex);
    const Column& neighborColumn = m_columns.at(configIndex + 1);
    const int neighborOldSize = header->sectionSize(neighborColumn.logicalIndex);
    const int requestedDelta = newSize - oldSize;

    int currentSize = newSize;
    int neighborSize = neighborOldSize - requestedDelta;
    if (neighborSize < neighborColumn.minimumWidth) {
        const int allowedDelta = neighborOldSize - neighborColumn.minimumWidth;
        currentSize = oldSize + allowedDelta;
        neighborSize = neighborColumn.minimumWidth;
    }
    if (currentSize < currentColumn.minimumWidth) {
        const int allowedDelta = oldSize - currentColumn.minimumWidth;
        currentSize = currentColumn.minimumWidth;
        neighborSize = neighborOldSize + allowedDelta;
    }

    const QSignalBlocker blocker(header);
    m_isBalancing = true;
    header->resizeSection(currentColumn.logicalIndex, currentSize);
    header->resizeSection(neighborColumn.logicalIndex, neighborSize);
    m_isBalancing = false;
    fitToViewport();
}

void ViewportFittedTableColumns::setColumnsChangedCallback(std::function<void()> callback)
{
    m_columnsChangedCallback = std::move(callback);
}

bool ViewportFittedTableColumns::eventFilter(QObject* watched, QEvent* event)
{
    if (m_tableView == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    if ((watched == m_tableView->viewport() && event->type() == QEvent::Resize) ||
        (watched == m_tableView && (event->type() == QEvent::Show || event->type() == QEvent::StyleChange ||
                                    event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange ||
                                    event->type() == QEvent::PolishRequest))) {
        scheduleFitToViewport();
    }
    if (watched == m_tableView && (event->type() == QEvent::Show || event->type() == QEvent::StyleChange)) {
        QTimer::singleShot(50, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    }
    return QObject::eventFilter(watched, event);
}

void ViewportFittedTableColumns::attachModel(QAbstractItemModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model != nullptr) {
        m_model->disconnect(this);
    }
    m_model = model;
    if (m_model == nullptr) {
        return;
    }

    connect(m_model, &QAbstractItemModel::modelReset, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    connect(m_model, &QAbstractItemModel::layoutChanged, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    connect(m_model, &QAbstractItemModel::columnsInserted, this, &ViewportFittedTableColumns::scheduleFitToViewport);
    connect(m_model, &QAbstractItemModel::columnsRemoved, this, &ViewportFittedTableColumns::scheduleFitToViewport);
}

void ViewportFittedTableColumns::scheduleFitToViewport()
{
    if (m_fitScheduled) {
        return;
    }

    m_fitScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_fitScheduled = false;
        fitToViewport();
    });
}

int ViewportFittedTableColumns::columnConfigIndex(int logicalIndex) const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns.at(i).logicalIndex == logicalIndex) {
            return i;
        }
    }
    return -1;
}

int ViewportFittedTableColumns::stretchColumnConfigIndex() const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns.at(i).stretch) {
            return i;
        }
    }
    return -1;
}

int ViewportFittedTableColumns::minimumWidthExcept(int excludedConfigIndex) const
{
    int width = 0;
    for (int i = 0; i < m_columns.size(); ++i) {
        if (i != excludedConfigIndex) {
            width += m_columns.at(i).minimumWidth;
        }
    }
    return width;
}

int ViewportFittedTableColumns::currentColumnWidth(const Column& column) const
{
    if (m_tableView == nullptr || m_tableView->horizontalHeader() == nullptr) {
        return column.defaultWidth;
    }

    const int width = m_tableView->horizontalHeader()->sectionSize(column.logicalIndex);
    return width <= 0 ? column.defaultWidth : width;
}

int ViewportFittedTableColumns::defaultWidthSum() const
{
    int width = 0;
    for (const Column& column : m_columns) {
        width += column.defaultWidth;
    }
    return width;
}
