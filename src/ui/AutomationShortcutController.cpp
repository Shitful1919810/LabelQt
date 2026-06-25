#include "ui/AutomationShortcutController.h"

#include "ui/ShortcutUtils.h"

#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>

#include <algorithm>
#include <utility>

AutomationShortcutController::AutomationShortcutController(QWidget* window, QObject* parent)
    : QObject(parent), m_window(window)
{
}

void AutomationShortcutController::setPreferences(labelqt::core::AppPreferences preferences)
{
    m_preferences = std::move(preferences);
}

void AutomationShortcutController::setScripts(QVector<labelqt::services::AutomationScript> scripts)
{
    m_scripts = std::move(scripts);
}

bool AutomationShortcutController::handleGlobalShortcut(QObject* watched, QEvent* event)
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

    const QKeySequence pressedSequence(labelqt::ui::normalizedShortcutKeyCombination(*keyEvent));
    const QString scriptId = scriptIdForSequence(pressedSequence);
    if (!scriptId.isEmpty()) {
        if (!isShortcutOverride && !keyEvent->isAutoRepeat()) {
            emit scriptTriggered(scriptId);
        }
        event->accept();
        return true;
    }

    const QString missingScriptId = missingScriptIdForSequence(pressedSequence);
    if (!missingScriptId.isEmpty()) {
        if (!isShortcutOverride && !keyEvent->isAutoRepeat()) {
            emit missingScriptTriggered(missingScriptId);
        }
        event->accept();
        return true;
    }

    return false;
}

QString AutomationShortcutController::scriptIdForSequence(const QKeySequence& sequence) const
{
    for (const labelqt::services::AutomationScript& script : m_scripts) {
        const QKeySequence shortcut = m_preferences.automationShortcuts().value(script.id);
        if (!shortcut.isEmpty() && sequence.matches(shortcut) == QKeySequence::ExactMatch) {
            return script.id;
        }
    }
    return {};
}

QString AutomationShortcutController::missingScriptIdForSequence(const QKeySequence& sequence) const
{
    for (auto it = m_preferences.automationShortcuts().constBegin();
         it != m_preferences.automationShortcuts().constEnd(); ++it) {
        const bool scriptExists = std::any_of(m_scripts.cbegin(), m_scripts.cend(),
                                              [&it](const auto& script) { return script.id == it.key(); });
        if (!scriptExists && sequence.matches(it.value()) == QKeySequence::ExactMatch) {
            return it.key();
        }
    }
    return {};
}
