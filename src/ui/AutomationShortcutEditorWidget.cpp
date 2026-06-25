#include "ui/AutomationShortcutEditorWidget.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <map>
#include <utility>

namespace {
constexpr int scriptColumn = 0;
constexpr int shortcutColumn = 1;
constexpr int scriptIdRole = Qt::UserRole + 1;
} // namespace

AutomationShortcutEditorWidget::AutomationShortcutEditorWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* descriptionLabel =
        new QLabel(tr("Assign shortcuts to automation scripts. Leave a shortcut empty to disable it."), this);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Automation script"), tr("Shortcut")});
    m_table->horizontalHeader()->setSectionResizeMode(scriptColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(shortcutColumn, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
}

void AutomationShortcutEditorWidget::setScripts(QVector<labelqt::services::AutomationScript> scripts)
{
    m_scripts = std::move(scripts);
}

void AutomationShortcutEditorWidget::loadShortcuts(const QJsonObject& shortcuts)
{
    m_table->setRowCount(0);

    QSet<QString> knownScriptIds;
    for (const labelqt::services::AutomationScript& script : std::as_const(m_scripts)) {
        knownScriptIds.insert(script.id);
        addScriptRow(script, shortcuts);
    }

    for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
        if (!knownScriptIds.contains(it.key())) {
            addMissingScriptRow(it.key(), it.value());
        }
    }
}

QJsonObject AutomationShortcutEditorWidget::shortcuts() const
{
    QJsonObject result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem* item = m_table->item(row, scriptColumn);
        const auto* shortcutEdit = qobject_cast<QKeySequenceEdit*>(m_table->cellWidget(row, shortcutColumn));
        if (item == nullptr || shortcutEdit == nullptr || shortcutEdit->keySequence().isEmpty()) {
            continue;
        }

        const QString scriptId = item->data(scriptIdRole).toString();
        if (!scriptId.isEmpty()) {
            result.insert(scriptId, shortcutEdit->keySequence().toString(QKeySequence::PortableText));
        }
    }
    return result;
}

QString AutomationShortcutEditorWidget::conflictText() const
{
    std::map<QString, QString> scriptNameByShortcut;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem* item = m_table->item(row, scriptColumn);
        const auto* shortcutEdit = qobject_cast<QKeySequenceEdit*>(m_table->cellWidget(row, shortcutColumn));
        if (item == nullptr || shortcutEdit == nullptr || shortcutEdit->keySequence().isEmpty()) {
            continue;
        }

        const QString shortcutText = shortcutEdit->keySequence().toString(QKeySequence::PortableText);
        const auto [existing, inserted] = scriptNameByShortcut.emplace(shortcutText, item->text());
        if (!inserted) {
            return tr("Shortcut %1 is assigned to both %2 and %3.").arg(shortcutText, existing->second, item->text());
        }
    }
    return {};
}

void AutomationShortcutEditorWidget::addScriptRow(const labelqt::services::AutomationScript& script,
                                                  const QJsonObject& shortcuts)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    const QString source = script.official ? tr("Official") : tr("Custom");
    auto* item = new QTableWidgetItem(tr("%1 / %2 / %3").arg(source, script.directoryName, script.name));
    item->setData(scriptIdRole, script.id);
    item->setToolTip(script.description);
    m_table->setItem(row, scriptColumn, item);

    const QKeySequence shortcut =
        QKeySequence::fromString(shortcuts.value(script.id).toString(), QKeySequence::PortableText);
    auto* shortcutEdit = new QKeySequenceEdit(shortcut, m_table);
    connect(shortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &AutomationShortcutEditorWidget::changed);
    m_table->setCellWidget(row, shortcutColumn, shortcutEdit);
}

void AutomationShortcutEditorWidget::addMissingScriptRow(const QString& scriptId, const QJsonValue& shortcutValue)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto* item = new QTableWidgetItem(tr("Missing script: %1").arg(scriptId));
    item->setData(scriptIdRole, scriptId);
    item->setForeground(QColor(QStringLiteral("#b26a00")));
    m_table->setItem(row, scriptColumn, item);

    const QKeySequence shortcut = QKeySequence::fromString(shortcutValue.toString(), QKeySequence::PortableText);
    auto* shortcutEdit = new QKeySequenceEdit(shortcut, m_table);
    connect(shortcutEdit, &QKeySequenceEdit::keySequenceChanged, this, &AutomationShortcutEditorWidget::changed);
    m_table->setCellWidget(row, shortcutColumn, shortcutEdit);
}
