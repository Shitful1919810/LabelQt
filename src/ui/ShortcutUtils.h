#pragma once

#include <QKeyEvent>

namespace labelqt::ui {

constexpr Qt::KeyboardModifiers shortcutModifierMask =
    Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;

inline QKeyCombination normalizedShortcutKeyCombination(const QKeyEvent& event)
{
    Qt::Key key = static_cast<Qt::Key>(event.key());
    Qt::KeyboardModifiers modifiers = event.modifiers() & shortcutModifierMask;
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        modifiers |= Qt::ShiftModifier;
    }
    return QKeyCombination(modifiers, key);
}

} // namespace labelqt::ui
