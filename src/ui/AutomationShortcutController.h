#pragma once

#include "core/AppPreferences.h"
#include "services/AutomationService.h"

#include <QObject>
#include <QString>
#include <QVector>

class QEvent;
class QWidget;

class AutomationShortcutController final : public QObject {
    Q_OBJECT

public:
    explicit AutomationShortcutController(QWidget* window, QObject* parent = nullptr);

    void setPreferences(labelqt::core::AppPreferences preferences);
    void setScripts(QVector<labelqt::services::AutomationScript> scripts);
    bool handleGlobalShortcut(QObject* watched, QEvent* event);

signals:
    void scriptTriggered(const QString& scriptId);
    void missingScriptTriggered(const QString& scriptId);

private:
    QString scriptIdForSequence(const QKeySequence& sequence) const;
    QString missingScriptIdForSequence(const QKeySequence& sequence) const;

    QWidget* m_window{nullptr};
    labelqt::core::AppPreferences m_preferences;
    QVector<labelqt::services::AutomationScript> m_scripts;
};
