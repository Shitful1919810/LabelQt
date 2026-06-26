#pragma once

#include "core/AppPreferences.h"
#include "core/Label.h"

#include <QColor>
#include <QFont>
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <optional>

class QGraphicsItem;
class QGraphicsOpacityEffect;
class QGraphicsRectItem;
class QGraphicsTextItem;
class QLabel;

class ImageCanvas final : public QGraphicsView {
    Q_OBJECT

public:
    enum class InteractionMode {
        Label,
        Selection,
    };

    enum class PointerInteractionState {
        Idle,
        PendingLabelCreate,
        PendingLabelSelect,
        PendingEmptyClick,
        PendingLabelMove,
        MovingLabel,
        SelectingRegion,
        MiddleButtonPanning,
    };

    explicit ImageCanvas(QWidget* parent = nullptr);
    ~ImageCanvas() override;

    void setInteractionMode(InteractionMode mode);
    InteractionMode interactionMode() const noexcept;
    void setReadOnly(bool readOnly);
    bool isReadOnly() const noexcept;
    void setPreferences(const labelqt::core::AppPreferences& preferences);
    void setImage(const QString& path, const QVector<labelqt::core::Label>& labels);
    void setImage(const QString& path, const QImage& image, const QVector<labelqt::core::Label>& labels);
    void setImageLoading(const QString& path, const QVector<labelqt::core::Label>& labels);
    void setLabels(const QVector<labelqt::core::Label>& labels);
    void setGroups(QStringList groups);
    void setVisibleGroups(QStringList groups);
    void setSelectedLabel(int index);
    void setSelectedLabels(QVector<int> indexes);
    void setSelectedLabelTextBubblesVisible(bool visible);
    void setLabelTextPreview(int index, const QString& text);
    void clearLabelTextPreview(int index);
    void centerOnLabel(int index);
    QPoint globalPositionForLabel(int index) const;
    bool hasSelection() const noexcept;
    QRectF normalizedSelectionRect() const noexcept;
    std::optional<QPointF> normalizedCursorImagePosition() const;
    void setZoomPercent(int percent);
    int zoomPercent() const noexcept;
    QPointF normalizedViewCenter() const;
    void restoreView(int zoomPercent, QPointF normalizedCenter);

signals:
    void labelCreateRequested(QPointF normalizedPosition);
    void labelMoveRequested(int index, QPointF normalizedPosition);
    void labelsMoveRequested(QVector<int> indexes, QVector<QPointF> normalizedPositions);
    void labelSelected(int index);
    void labelClicked(int index, Qt::KeyboardModifiers modifiers);
    void emptyAreaClicked();
    void labelTextEditRequested(int index, QPoint globalPosition);
    void deleteRequested();
    void zoomPercentChanged(int percent);
    void viewportStateChanged(int zoomPercent, QPointF normalizedCenter);
    void selectionChanged(QRectF normalizedRect);
    void imageCopiedToClipboard();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void clearSceneItems();
    void showSceneMessage(const QString& message);
    void rebuildLabelItems();
    void applyZoom();
    void updateScenePadding();
    void setZoomPercentAt(int percent, QPointF sceneAnchor, QPointF globalAnchor);
    void notifyViewportStateChanged();
    bool isLabelVisible(const labelqt::core::Label& label) const;
    bool hasMoveLabelModifiers(Qt::KeyboardModifiers modifiers) const;
    int displayNumberForLabel(int index) const;
    QString displayTextForLabel(int index) const;
    void updateHoveredLabelToolTip(const QPoint& viewportPosition, const QPoint& globalPosition);
    void hideHoveredLabelToolTip();
    labelqt::core::LabelGroupStyle styleForGroup(const QString& group) const;
    QPointF normalizedPositionFromScene(QPointF scenePosition) const;
    QRectF imageClampedSceneRect(QPointF firstScenePosition, QPointF secondScenePosition) const;
    QRectF normalizedRectFromSceneRect(QRectF sceneRect) const;
    void beginSelection(QPoint viewportPosition);
    void updateSelection(QPoint viewportPosition);
    void finishSelection(QPoint viewportPosition);
    void clearSelection();
    bool copyImageToClipboard();
    void updateCursorForInteractionMode();
    void resetPointerInteraction();
    QVector<QPointF> movingLabelPositionsForViewportPosition(QPoint viewportPosition) const;
    void previewMovingLabels(QPoint viewportPosition);

    QGraphicsScene m_scene;
    QGraphicsPixmapItem* m_pixmapItem{nullptr};
    QGraphicsRectItem* m_selectionItem{nullptr};
    QGraphicsTextItem* m_statusTextItem{nullptr};
    QLabel* m_hoverToolTip{nullptr};
    QGraphicsOpacityEffect* m_hoverOpacityEffect{nullptr};
    QVector<labelqt::core::Label> m_labels;
    QVector<QGraphicsItem*> m_labelItems;
    QString m_imagePath;
    QSet<int> m_selectedLabels;
    bool m_selectedLabelTextBubblesVisible{true};
    int m_zoomPercent{100};
    double m_markerDiameterPixels{20.0};
    double m_markerFontPointSize{10.0};
    double m_textBubbleOpacity{1.0};
    QFont m_textBubbleFont;
    QHash<int, QString> m_labelTextPreviews;
    Qt::KeyboardModifiers m_moveLabelModifiers{Qt::ControlModifier};
    QStringList m_groups;
    QSet<QString> m_visibleGroups;
    QVector<labelqt::core::LabelGroupStyle> m_groupStyles;
    bool m_hasUserZoom{false};
    bool m_isDestroying{false};
    bool m_readOnly{false};
    PointerInteractionState m_pointerState{PointerInteractionState::Idle};
    int m_pendingLabelSelectIndex{-1};
    int m_movingLabelIndex{-1};
    int m_pendingLabelMoveIndex{-1};
    Qt::KeyboardModifiers m_pendingLabelSelectModifiers{Qt::NoModifier};
    QVector<int> m_movingLabelIndexes;
    QVector<QPointF> m_movingLabelStartPositions;
    QPointF m_movingLabelAnchorStartPosition;
    std::optional<QPointF> m_wheelZoomSceneAnchor;
    QPointF m_wheelZoomGlobalAnchor;
    QPoint m_labelCreatePressPosition;
    QPoint m_labelSelectPressPosition;
    QPoint m_emptyClickPressPosition;
    QPoint m_lastMiddlePanPosition;
    QPointF m_selectionStartScenePosition;
    QRectF m_normalizedSelectionRect;
    InteractionMode m_interactionMode{InteractionMode::Label};
};
