#pragma once

#include "core/AppPreferences.h"

#include <QObject>

#include <functional>

class QEvent;
class QWidget;

class MainWindowShortcutController final : public QObject {
    Q_OBJECT

public:
    struct Callbacks {
        std::function<void()> selectPreviousLabel;
        std::function<void()> selectNextLabel;
        std::function<void()> selectPreviousPage;
        std::function<void()> selectNextPage;
        std::function<void()> editCurrentLabelText;
    };

    explicit MainWindowShortcutController(QWidget* window, QObject* parent = nullptr);

    void setPreferences(labelqt::core::AppPreferences preferences);
    void setCallbacks(Callbacks callbacks);
    bool handleGlobalShortcut(QObject* watched, QEvent* event);
    bool handleLabelViewShortcut(QEvent* event);

private:
    QWidget* m_window{nullptr};
    labelqt::core::AppPreferences m_preferences;
    Callbacks m_callbacks;
};
