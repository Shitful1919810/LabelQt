#include "ui/LabelTableView.h"

#include <QAbstractItemModel>
#include <QCursor>
#include <QDrag>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>

#include <algorithm>

LabelTableView::LabelTableView(QWidget* parent) : QTableView(parent) {}

void LabelTableView::dragMoveEvent(QDragMoveEvent* event)
{
    if (model() == nullptr || event == nullptr) {
        QTableView::dragMoveEvent(event);
        return;
    }

    const int row = insertionRowForPosition(event->position().toPoint());
    if (model()->canDropMimeData(event->mimeData(), Qt::MoveAction, row, 0, QModelIndex())) {
        setDropIndicatorRow(row);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    clearDropIndicator();
    QTableView::dragMoveEvent(event);
}

void LabelTableView::dragLeaveEvent(QDragLeaveEvent* event)
{
    clearDropIndicator();
    QTableView::dragLeaveEvent(event);
}

void LabelTableView::dropEvent(QDropEvent* event)
{
    if (model() == nullptr || event == nullptr) {
        clearDropIndicator();
        QTableView::dropEvent(event);
        return;
    }

    const int row = insertionRowForPosition(event->position().toPoint());
    if (model()->dropMimeData(event->mimeData(), Qt::MoveAction, row, 0, QModelIndex())) {
        clearDropIndicator();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    clearDropIndicator();
    QTableView::dropEvent(event);
}

void LabelTableView::paintEvent(QPaintEvent* event)
{
    QTableView::paintEvent(event);

    if (m_dropIndicatorRow < 0 || model() == nullptr || viewport() == nullptr) {
        return;
    }

    const int y = insertionLineY(m_dropIndicatorRow);
    if (y < 0) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);

    constexpr int outlineWidth = 7;
    constexpr int lineWidth = 5;
    constexpr int triangleWidth = 10;
    constexpr int triangleHeight = 8;
    const QColor lineColor(QStringLiteral("#facc15"));
    const QColor outlineColor(0, 0, 0, 180);

    QPen outlinePen(outlineColor, outlineWidth);
    outlinePen.setCapStyle(Qt::SquareCap);
    painter.setPen(outlinePen);
    painter.drawLine(QPoint(0, y), QPoint(viewport()->width(), y));

    QPen pen(lineColor, lineWidth);
    pen.setCapStyle(Qt::SquareCap);
    painter.setPen(pen);
    painter.drawLine(QPoint(0, y), QPoint(viewport()->width(), y));

    painter.setPen(outlineColor);
    painter.setBrush(lineColor);
    const QPolygon leftTriangle{
        QPoint(0, y - triangleHeight),
        QPoint(triangleWidth, y),
        QPoint(0, y + triangleHeight),
    };
    const QPolygon rightTriangle{
        QPoint(viewport()->width() - 1, y - triangleHeight),
        QPoint(viewport()->width() - triangleWidth - 1, y),
        QPoint(viewport()->width() - 1, y + triangleHeight),
    };
    painter.drawPolygon(leftTriangle);
    painter.drawPolygon(rightTriangle);
}

void LabelTableView::startDrag(Qt::DropActions supportedActions)
{
    if (model() == nullptr || selectionModel() == nullptr) {
        QTableView::startDrag(supportedActions);
        return;
    }

    const QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) {
        return;
    }

    QMimeData* mimeData = model()->mimeData(indexes);
    if (mimeData == nullptr) {
        return;
    }

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    QPoint hotSpot;
    const QPixmap pixmap = selectedRowsDragPixmap(&hotSpot);
    if (!pixmap.isNull()) {
        drag->setPixmap(pixmap);
        drag->setHotSpot(hotSpot);
    }

    drag->exec(supportedActions, defaultDropAction());
    clearDropIndicator();
}

int LabelTableView::insertionRowForPosition(QPoint position) const
{
    if (model() == nullptr) {
        return 0;
    }

    const int rows = model()->rowCount(rootIndex());
    if (rows <= 0) {
        return 0;
    }

    for (int row = 0; row < rows; ++row) {
        const QRect rect = visualRect(model()->index(row, 0, rootIndex()));
        if (!rect.isValid()) {
            continue;
        }
        if (position.y() < rect.center().y()) {
            return row;
        }
        if (position.y() <= rect.bottom() + 1) {
            return row + 1;
        }
    }

    return rows;
}

QPixmap LabelTableView::selectedRowsDragPixmap(QPoint* hotSpot) const
{
    if (selectionModel() == nullptr || viewport() == nullptr) {
        return {};
    }

    QRect sourceRect;
    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& rowIndex : rows) {
        const QRect rowRect = visualRect(model()->index(rowIndex.row(), 0, rootIndex())).united(
            visualRect(model()->index(rowIndex.row(), model()->columnCount(rootIndex()) - 1, rootIndex())));
        sourceRect = sourceRect.isNull() ? rowRect : sourceRect.united(rowRect);
    }
    if (sourceRect.isNull() || !sourceRect.intersects(viewport()->rect())) {
        return {};
    }

    sourceRect = sourceRect.intersected(viewport()->rect());
    QPixmap source(sourceRect.size());
    source.fill(Qt::transparent);
    {
        QPainter sourcePainter(&source);
        viewport()->render(&sourcePainter, QPoint(), QRegion(sourceRect));
    }

    QPixmap transparent(source.size());
    transparent.fill(Qt::transparent);
    {
        QPainter painter(&transparent);
        painter.setOpacity(0.62);
        painter.drawPixmap(0, 0, source);
    }

    if (hotSpot != nullptr) {
        const QPoint cursorPosition = viewport()->mapFromGlobal(QCursor::pos());
        *hotSpot = cursorPosition - sourceRect.topLeft();
    }
    return transparent;
}

int LabelTableView::insertionLineY(int row) const
{
    if (model() == nullptr) {
        return -1;
    }

    const int rows = model()->rowCount(rootIndex());
    if (rows <= 0) {
        return 0;
    }

    row = std::clamp(row, 0, rows);
    if (row <= 0) {
        const QRect firstRect = visualRect(model()->index(0, 0, rootIndex()));
        return firstRect.isValid() ? std::max(0, firstRect.top()) : 0;
    }
    if (row >= rows) {
        const QRect lastRect = visualRect(model()->index(rows - 1, 0, rootIndex()));
        return lastRect.isValid() ? std::min(viewport()->height() - 1, lastRect.bottom() + 1)
                                  : viewport()->height() - 1;
    }

    const QRect rect = visualRect(model()->index(row, 0, rootIndex()));
    return rect.isValid() ? rect.top() : -1;
}

void LabelTableView::setDropIndicatorRow(int row)
{
    if (m_dropIndicatorRow == row) {
        return;
    }
    m_dropIndicatorRow = row;
    if (viewport() != nullptr) {
        viewport()->update();
    }
}

void LabelTableView::clearDropIndicator()
{
    if (m_dropIndicatorRow < 0) {
        return;
    }
    m_dropIndicatorRow = -1;
    if (viewport() != nullptr) {
        viewport()->update();
    }
}
