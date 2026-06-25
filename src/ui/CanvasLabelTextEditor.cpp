#include "ui/CanvasLabelTextEditor.h"

#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {
class CanvasPlainTextEdit final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        QPlainTextEdit::wheelEvent(event);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QPlainTextEdit::mousePressEvent(event);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QPlainTextEdit::mouseMoveEvent(event);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QPlainTextEdit::mouseReleaseEvent(event);
        event->accept();
    }
};
} // namespace

CanvasLabelTextEditor::CanvasLabelTextEditor(QWidget* parent) : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QFrame {"
                                 "background: palette(base);"
                                 "border: 1px solid palette(mid);"
                                 "border-radius: 3px;"
                                 "}"));
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);

    m_editor = new CanvasPlainTextEdit(this);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setTabChangesFocus(true);
    layout->addWidget(m_editor);

    connect(m_editor, &QPlainTextEdit::textChanged, this,
            [this]() { emit textChanged(m_editor != nullptr ? m_editor->toPlainText() : QString()); });
}

void CanvasLabelTextEditor::setText(const QString& text)
{
    m_editor->setPlainText(text);
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cursor);
}

QString CanvasLabelTextEditor::text() const
{
    return m_editor != nullptr ? m_editor->toPlainText() : QString();
}

void CanvasLabelTextEditor::setEditorFont(const QFont& font)
{
    m_editor->setFont(font);
}

void CanvasLabelTextEditor::setEditorOpacity(double opacity)
{
    if (m_opacityEffect != nullptr) {
        m_opacityEffect->setOpacity(std::clamp(opacity, 0.0, 1.0));
    }
}

void CanvasLabelTextEditor::moveNearGlobalPosition(const QPoint& globalPosition)
{
    QWidget* parent = parentWidget();
    if (parent == nullptr) {
        return;
    }

    const QSize viewportSize = parent->size();
    const QSize maximumSize(std::max(1, viewportSize.width() - 12), std::max(1, viewportSize.height() - 12));
    resize(QSize(320, 140)
               .boundedTo(maximumSize)
               .expandedTo(QSize(std::min(220, maximumSize.width()), std::min(96, maximumSize.height()))));

    QPoint localPosition = parent->mapFromGlobal(globalPosition + QPoint(12, 18));
    localPosition.setX(std::clamp(localPosition.x(), 0, std::max(0, viewportSize.width() - width())));
    localPosition.setY(std::clamp(localPosition.y(), 0, std::max(0, viewportSize.height() - height())));
    move(localPosition);
}

QPlainTextEdit* CanvasLabelTextEditor::editor() const noexcept
{
    return m_editor;
}

void CanvasLabelTextEditor::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void CanvasLabelTextEditor::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void CanvasLabelTextEditor::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}

void CanvasLabelTextEditor::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}
