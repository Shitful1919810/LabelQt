#pragma once

#include <QTableView>

class QDragMoveEvent;
class QDropEvent;
class QDragLeaveEvent;
class QFocusEvent;
class QPaintEvent;

class LabelTableView final : public QTableView {
    Q_OBJECT

public:
    explicit LabelTableView(QWidget* parent = nullptr);

protected:
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    int insertionRowForPosition(QPoint position) const;
    int insertionLineY(int row) const;
    QPixmap selectedRowsDragPixmap(QPoint* hotSpot) const;
    void refreshSelectionAppearance();
    void setDropIndicatorRow(int row);
    void clearDropIndicator();

    int m_dropIndicatorRow{-1};
};
