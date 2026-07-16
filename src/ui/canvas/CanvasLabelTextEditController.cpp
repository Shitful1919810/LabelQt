#include "ui/canvas/CanvasLabelTextEditController.h"

#include "ui/canvas/CanvasLabelTextEditor.h"
#include "ui/canvas/ImageCanvas.h"
#include "ui/ShortcutUtils.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTimer>

#include <utility>

CanvasLabelTextEditController::CanvasLabelTextEditController(QObject* parent) : QObject(parent) {}

CanvasLabelTextEditController::~CanvasLabelTextEditController()
{
    tearDownEditor(false);
}

void CanvasLabelTextEditController::setCommitShortcut(QKeySequence shortcut)
{
    if (!shortcut.isEmpty()) {
        m_commitShortcut = std::move(shortcut);
    }
}

void CanvasLabelTextEditController::setEditorOpacity(double opacity)
{
    m_editorOpacity = std::clamp(opacity, 0.0, 1.0);
    if (m_editor != nullptr) {
        m_editor->setEditorOpacity(m_editorOpacity);
    }
}

bool CanvasLabelTextEditController::isEditing() const noexcept
{
    return m_editor != nullptr;
}

bool CanvasLabelTextEditController::isEditorObject(QObject* object) const noexcept
{
    return m_editor != nullptr && object == m_editor->editor();
}

bool CanvasLabelTextEditController::hasEditorFocus() const noexcept
{
    return m_editor != nullptr && m_editor->editor() == QApplication::focusWidget();
}

int CanvasLabelTextEditController::imageIndex() const noexcept
{
    return m_imageIndex;
}

int CanvasLabelTextEditController::labelIndex() const noexcept
{
    return m_labelIndex;
}

void CanvasLabelTextEditController::open(ImageCanvas* canvas, int imageIndex, int labelIndex, const QString& text,
                                         const QFont& font)
{
    close();
    if (canvas == nullptr) {
        return;
    }

    auto* editor = new CanvasLabelTextEditor(canvas->viewport());
    editor->setEditorOpacity(m_editorOpacity);
    editor->setEditorFont(font);
    editor->setText(text);
    editor->editor()->installEventFilter(this);
    qApp->installEventFilter(this);

    m_canvas = canvas;
    m_editor = editor;
    m_imageIndex = imageIndex;
    m_labelIndex = labelIndex;

    m_viewportStateConnection = connect(canvas, &ImageCanvas::viewportStateChanged, this, [this]() {
        repositionEditor();
        scheduleEditorReposition();
    });

    connect(editor, &CanvasLabelTextEditor::textChanged, this, [this](const QString& changedText) {
        if (m_editor != nullptr) {
            emit previewTextChanged(m_labelIndex, changedText);
        }
    });

    repositionEditor();
    editor->show();
    editor->raise();
    editor->editor()->setFocus(Qt::MouseFocusReason);
}

void CanvasLabelTextEditController::commit()
{
    if (m_editor == nullptr) {
        close();
        return;
    }

    const int imageIndex = m_imageIndex;
    const int labelIndex = m_labelIndex;
    const QString text = m_editor->text();
    close();
    emit textCommitted(imageIndex, labelIndex, text);
}

void CanvasLabelTextEditController::cancel()
{
    close();
}

void CanvasLabelTextEditController::close()
{
    tearDownEditor(true);
}

void CanvasLabelTextEditController::tearDownEditor(bool notifyClosed)
{
    const int labelIndex = m_labelIndex;

    disconnect(m_viewportStateConnection);
    m_viewportStateConnection = {};
    m_canvas = nullptr;

    if (m_editor != nullptr) {
        m_editor->editor()->removeEventFilter(this);
        qApp->removeEventFilter(this);
        m_editor->deleteLater();
    }

    m_editor = nullptr;
    m_imageIndex = -1;
    m_labelIndex = -1;
    m_editorRepositionScheduled = false;

    if (notifyClosed && labelIndex >= 0) {
        emit closed(labelIndex);
    }
}

void CanvasLabelTextEditController::repositionEditor()
{
    if (m_canvas == nullptr || m_editor == nullptr || m_labelIndex < 0) {
        return;
    }

    m_editor->moveNearGlobalPosition(m_canvas->globalPositionForLabel(m_labelIndex));
}

void CanvasLabelTextEditController::scheduleEditorReposition()
{
    if (m_editorRepositionScheduled) {
        return;
    }

    m_editorRepositionScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_editorRepositionScheduled = false;
        repositionEditor();
    });
}

bool CanvasLabelTextEditController::eventFilter(QObject* watched, QEvent* event)
{
    if (m_canvas != nullptr && watched == m_canvas->viewport() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        scheduleEditorReposition();
    }

    if (m_editor != nullptr &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick ||
         event->type() == QEvent::WindowDeactivate)) {
        const auto* watchedWidget = qobject_cast<QWidget*>(watched);
        bool insideEditor =
            watchedWidget != nullptr && (watchedWidget == m_editor || m_editor->isAncestorOf(watchedWidget));
        if (!insideEditor &&
            (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint editorPosition = m_editor->mapFromGlobal(mouseEvent->globalPosition().toPoint());
            insideEditor = m_editor->rect().contains(editorPosition);
        }

        if (!insideEditor) {
            QTimer::singleShot(0, this, [this]() {
                if (m_editor != nullptr) {
                    commit();
                }
            });
        }
    }

    if (!isEditorObject(watched)) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        const QKeySequence keySequence(labelqt::ui::normalizedShortcutKeyCombination(*keyEvent));
        if (keySequence.matches(m_commitShortcut) == QKeySequence::ExactMatch) {
            commit();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
    }

    if (event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, [this]() {
            if (m_editor != nullptr && !m_editor->isAncestorOf(QApplication::focusWidget())) {
                commit();
            }
        });
    }

    return QObject::eventFilter(watched, event);
}
