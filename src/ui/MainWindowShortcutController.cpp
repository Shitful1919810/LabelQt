#include "ui/MainWindowShortcutController.h"

#include "ui/ShortcutUtils.h"

#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <utility>

namespace {
QKeyCombination firstKeyCombination(const QKeySequence& sequence)
{
    return sequence.isEmpty() ? QKeyCombination() : sequence[0];
}

QKeyCombination withModifiers(QKeyCombination combination, Qt::KeyboardModifiers modifiers)
{
    if (combination.key() == Qt::Key_unknown) {
        return {};
    }
    return QKeyCombination(combination.keyboardModifiers() | modifiers, combination.key());
}

bool hasSameKeyIgnoringModifiers(QKeyCombination lhs, QKeyCombination rhs)
{
    return lhs.key() != Qt::Key_unknown && lhs.key() == rhs.key();
}

bool isTextInputWidget(const QWidget* widget)
{
    return qobject_cast<const QLineEdit*>(widget) != nullptr || qobject_cast<const QTextEdit*>(widget) != nullptr ||
           qobject_cast<const QPlainTextEdit*>(widget) != nullptr;
}
} // namespace

MainWindowShortcutController::MainWindowShortcutController(QWidget* window, QObject* parent)
    : QObject(parent), m_window(window)
{
}

void MainWindowShortcutController::setPreferences(labelqt::core::AppPreferences preferences)
{
    m_preferences = std::move(preferences);
}

void MainWindowShortcutController::setCallbacks(Callbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

bool MainWindowShortcutController::handleGlobalShortcut(QObject* watched, QEvent* event)
{
    const bool isShortcutOverride = event->type() == QEvent::ShortcutOverride;
    if (event->type() != QEvent::KeyPress && !isShortcutOverride) {
        return false;
    }

    const auto* watchedWidget = qobject_cast<QWidget*>(watched);
    if (watchedWidget == nullptr || watchedWidget->window() != m_window) {
        return false;
    }

    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_unknown) {
        return false;
    }

    const QKeyCombination pressedKey = labelqt::ui::normalizedShortcutKeyCombination(*keyEvent);
    const QKeySequence keySequence(pressedKey);

    if (!isTextInputWidget(watchedWidget)) {
        if (keySequence.matches(QKeySequence::Cut) == QKeySequence::ExactMatch) {
            if (!isShortcutOverride && !keyEvent->isAutoRepeat() && m_callbacks.cutSelectedLabels) {
                m_callbacks.cutSelectedLabels();
            }
            event->accept();
            return true;
        }

        if (keySequence.matches(QKeySequence::Copy) == QKeySequence::ExactMatch) {
            if (!isShortcutOverride && !keyEvent->isAutoRepeat() && m_callbacks.copySelectedLabels) {
                m_callbacks.copySelectedLabels();
            }
            event->accept();
            return true;
        }

        if (keySequence.matches(QKeySequence::Paste) == QKeySequence::ExactMatch) {
            if (!isShortcutOverride && !keyEvent->isAutoRepeat() && m_callbacks.pasteLabels) {
                m_callbacks.pasteLabels();
            }
            event->accept();
            return true;
        }
    }

    const QKeyCombination nextLabelKey = firstKeyCombination(m_preferences.nextLabelShortcut());
    const QKeyCombination previousLabelKey = withModifiers(nextLabelKey, m_preferences.previousLabelModifiers());
    const QKeyCombination alternatePreviousLabelKey =
        firstKeyCombination(m_preferences.alternatePreviousLabelShortcut());
    const QKeyCombination alternateNextLabelKey = firstKeyCombination(m_preferences.alternateNextLabelShortcut());

    if (pressedKey == alternatePreviousLabelKey) {
        if (!isShortcutOverride && m_callbacks.selectPreviousLabel) {
            m_callbacks.selectPreviousLabel();
        }
        event->accept();
        return true;
    }

    if (pressedKey == alternateNextLabelKey) {
        if (!isShortcutOverride && m_callbacks.selectNextLabel) {
            m_callbacks.selectNextLabel();
        }
        event->accept();
        return true;
    }

    if (pressedKey == previousLabelKey && previousLabelKey != nextLabelKey) {
        if (!isShortcutOverride && m_callbacks.selectPreviousLabel) {
            m_callbacks.selectPreviousLabel();
        }
        event->accept();
        return true;
    }

    if (pressedKey == nextLabelKey) {
        if (!isShortcutOverride && m_callbacks.selectNextLabel) {
            m_callbacks.selectNextLabel();
        }
        event->accept();
        return true;
    }

    if (hasSameKeyIgnoringModifiers(pressedKey, nextLabelKey)) {
        event->accept();
        return true;
    }

    if (keySequence.matches(m_preferences.previousPageShortcut()) == QKeySequence::ExactMatch) {
        if (!isShortcutOverride && m_callbacks.selectPreviousPage) {
            m_callbacks.selectPreviousPage();
        }
        event->accept();
        return true;
    }

    if (keySequence.matches(m_preferences.nextPageShortcut()) == QKeySequence::ExactMatch) {
        if (!isShortcutOverride && m_callbacks.selectNextPage) {
            m_callbacks.selectNextPage();
        }
        event->accept();
        return true;
    }

    return false;
}

bool MainWindowShortcutController::handleLabelViewShortcut(QEvent* event)
{
    const bool isShortcutOverride = event->type() == QEvent::ShortcutOverride;
    if (event->type() != QEvent::KeyPress && !isShortcutOverride) {
        return false;
    }

    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_unknown || isShortcutOverride || keyEvent->isAutoRepeat()) {
        return false;
    }

    const QKeyCombination pressedKey = labelqt::ui::normalizedShortcutKeyCombination(*keyEvent);
    const QKeySequence keySequence(pressedKey);
    if (keySequence.matches(m_preferences.editLabelTextShortcut()) == QKeySequence::ExactMatch) {
        if (m_callbacks.editCurrentLabelText) {
            m_callbacks.editCurrentLabelText();
        }
        return true;
    }

    return false;
}
