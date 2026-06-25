#include "ui/CanvasLabelTextEditController.h"

#include "ui/CanvasLabelTextEditor.h"
#include "ui/ShortcutUtils.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTimer>

#include <utility>

CanvasLabelTextEditController::CanvasLabelTextEditController(QObject* parent) : QObject(parent) {}

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

void CanvasLabelTextEditController::open(QWidget* parent, int imageIndex, int labelIndex, const QString& text,
                                         const QFont& font, const QPoint& globalPosition)
{
    close();

    auto* editor = new CanvasLabelTextEditor(parent);
    editor->setEditorOpacity(m_editorOpacity);
    editor->setEditorFont(font);
    editor->setText(text);
    editor->editor()->installEventFilter(this);
    qApp->installEventFilter(this);

    m_editor = editor;
    m_imageIndex = imageIndex;
    m_labelIndex = labelIndex;

    connect(editor, &CanvasLabelTextEditor::textChanged, this, [this](const QString& changedText) {
        if (m_editor != nullptr) {
            emit previewTextChanged(m_labelIndex, changedText);
        }
    });

    editor->moveNearGlobalPosition(globalPosition);
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
    const int labelIndex = m_labelIndex;

    if (m_editor != nullptr) {
        m_editor->editor()->removeEventFilter(this);
        qApp->removeEventFilter(this);
        m_editor->deleteLater();
    }

    m_editor = nullptr;
    m_imageIndex = -1;
    m_labelIndex = -1;

    if (labelIndex >= 0) {
        emit closed(labelIndex);
    }
}

bool CanvasLabelTextEditController::eventFilter(QObject* watched, QEvent* event)
{
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
