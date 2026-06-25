#pragma once

#include "services/AutomationService.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QWidget>

class QTableWidget;

class AutomationShortcutEditorWidget final : public QWidget {
    Q_OBJECT

public:
    explicit AutomationShortcutEditorWidget(QWidget* parent = nullptr);

    void setScripts(QVector<labelqt::services::AutomationScript> scripts);
    void loadShortcuts(const QJsonObject& shortcuts);
    QJsonObject shortcuts() const;
    QString conflictText() const;

signals:
    void changed();

private:
    void addScriptRow(const labelqt::services::AutomationScript& script, const QJsonObject& shortcuts);
    void addMissingScriptRow(const QString& scriptId, const QJsonValue& shortcutValue);

    QTableWidget* m_table{nullptr};
    QVector<labelqt::services::AutomationScript> m_scripts;
};
