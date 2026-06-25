#include "ui/EditorStateController.h"

#include "ui/CanvasLabelTextEditController.h"

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QComboBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTextCursor>

namespace {
template <typename T>
T* selfOrAncestor(QWidget* widget)
{
    while (widget != nullptr) {
        if (auto* result = qobject_cast<T*>(widget)) {
            return result;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}
} // namespace

EditorStateController::EditorStateController(QObject* parent) : QObject(parent) {}

void EditorStateController::setWidgets(QPlainTextEdit* bottomEditor, QTableView* labelView,
                                       CanvasLabelTextEditController* canvasTextEditController)
{
    m_bottomEditor = bottomEditor;
    m_labelView = labelView;
    m_canvasTextEditController = canvasTextEditController;
}

void EditorStateController::setCallbacks(std::function<void()> commitCanvasTextEditor,
                                         std::function<void()> commitBottomEditor, std::function<void()> editTableText,
                                         std::function<void()> openCanvasTextEditor)
{
    m_commitCanvasTextEditor = std::move(commitCanvasTextEditor);
    m_commitBottomEditor = std::move(commitBottomEditor);
    m_editTableText = std::move(editTableText);
    m_openCanvasTextEditor = std::move(openCanvasTextEditor);
}

EditorStateController::Mode EditorStateController::activeMode() const
{
    QWidget* focusWidget = QApplication::focusWidget();
    auto* focusedTextEdit = selfOrAncestor<QPlainTextEdit>(focusWidget);
    if (m_canvasTextEditController != nullptr && m_canvasTextEditController->hasEditorFocus()) {
        return Mode::CanvasTextEditor;
    }
    if (focusedTextEdit == m_bottomEditor) {
        return Mode::BottomEditor;
    }
    if (focusedTextEdit != nullptr && m_labelView != nullptr && m_labelView->isAncestorOf(focusedTextEdit)) {
        return Mode::TableTextEditor;
    }
    return Mode::None;
}

void EditorStateController::commitActive()
{
    if (m_canvasTextEditController != nullptr && m_canvasTextEditController->hasEditorFocus()) {
        if (m_commitCanvasTextEditor) {
            m_commitCanvasTextEditor();
        }
        return;
    }

    if (QApplication::focusWidget() == m_bottomEditor ||
        selfOrAncestor<QPlainTextEdit>(QApplication::focusWidget()) == m_bottomEditor) {
        if (m_commitBottomEditor) {
            m_commitBottomEditor();
        }
        return;
    }

    QWidget* itemEditor = activeItemEditor();
    if (itemEditor == nullptr || m_labelView == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(m_labelView, "commitData", Qt::DirectConnection, Q_ARG(QWidget*, itemEditor));
    QMetaObject::invokeMethod(m_labelView, "closeEditor", Qt::DirectConnection, Q_ARG(QWidget*, itemEditor),
                              Q_ARG(QAbstractItemDelegate::EndEditHint, QAbstractItemDelegate::NoHint));
}

void EditorStateController::restoreAfterNavigation(Mode mode, bool hasCurrentLabel)
{
    if (!hasCurrentLabel) {
        return;
    }

    switch (mode) {
    case Mode::BottomEditor:
        if (m_bottomEditor != nullptr && m_bottomEditor->isEnabled()) {
            m_bottomEditor->setFocus(Qt::ShortcutFocusReason);
            m_bottomEditor->moveCursor(QTextCursor::End);
        }
        break;
    case Mode::TableTextEditor:
        if (m_editTableText) {
            m_editTableText();
        }
        break;
    case Mode::CanvasTextEditor:
        if (m_openCanvasTextEditor) {
            m_openCanvasTextEditor();
        }
        break;
    case Mode::None:
        break;
    }
}

QWidget* EditorStateController::activeItemEditor() const
{
    if (m_labelView == nullptr) {
        return nullptr;
    }

    QWidget* focusWidget = QApplication::focusWidget();
    if (auto* focusedTextEdit = selfOrAncestor<QPlainTextEdit>(focusWidget);
        focusedTextEdit != nullptr && m_labelView->isAncestorOf(focusedTextEdit)) {
        return focusedTextEdit;
    }
    if (auto* focusedComboBox = selfOrAncestor<QComboBox>(focusWidget);
        focusedComboBox != nullptr && m_labelView->isAncestorOf(focusedComboBox)) {
        return focusedComboBox;
    }
    return nullptr;
}
