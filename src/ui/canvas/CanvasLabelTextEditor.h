#pragma once

#include <QFrame>

class QPlainTextEdit;
class QGraphicsOpacityEffect;
class QMouseEvent;
class QWheelEvent;

class CanvasLabelTextEditor final : public QFrame {
    Q_OBJECT

public:
    explicit CanvasLabelTextEditor(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const;
    void setEditorFont(const QFont& font);
    void setEditorOpacity(double opacity);
    void moveNearGlobalPosition(const QPoint& globalPosition);
    QPlainTextEdit* editor() const noexcept;

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void textChanged(QString text);

private:
    QPlainTextEdit* m_editor{nullptr};
    QGraphicsOpacityEffect* m_opacityEffect{nullptr};
};
