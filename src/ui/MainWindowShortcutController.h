#pragma once

#include "core/AppPreferences.h"

#include <QObject>
#include <QPointer>
#include <QVector>

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
        std::function<void()> copySelectedLabels;
        std::function<void()> cutSelectedLabels;
        std::function<void()> pasteLabels;
        std::function<void()> deleteSelectedLabels;
    };

    explicit MainWindowShortcutController(QWidget* window, QObject* parent = nullptr);

    void setPreferences(labelqt::core::AppPreferences preferences);
    void setCallbacks(Callbacks callbacks);
    void setLabelWorkspaceWidgets(QVector<QWidget*> widgets);
    bool handleGlobalShortcut(QObject* watched, QEvent* event);
    bool handleLabelViewShortcut(QEvent* event);

private:
    QPointer<QWidget> m_window;
    QVector<QPointer<QWidget>> m_labelWorkspaceWidgets;
    labelqt::core::AppPreferences m_preferences;
    Callbacks m_callbacks;
};
