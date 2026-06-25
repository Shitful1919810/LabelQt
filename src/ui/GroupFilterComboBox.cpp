#include "ui/GroupFilterComboBox.h"

#include <QAction>
#include <QCheckBox>
#include <QTimer>
#include <QWidgetAction>

#include <utility>

GroupFilterComboBox::GroupFilterComboBox(QWidget* parent) : QToolButton(parent), m_menu(this)
{
    setPopupMode(QToolButton::InstantPopup);
    setMenu(&m_menu);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    updateButtonText();
}

void GroupFilterComboBox::setGroups(const QStringList& groups, QVector<labelqt::core::LabelGroupStyle> groupStyles)
{
    m_groups = groups;
    m_groupStyles = std::move(groupStyles);
    m_selectedGroups.clear();
    for (const QString& group : m_groups) {
        m_selectedGroups.insert(group);
    }
    rebuildMenu();
    updateButtonText();
}

QStringList GroupFilterComboBox::selectedGroups() const
{
    QStringList groups;
    for (const QString& group : m_groups) {
        if (m_selectedGroups.contains(group)) {
            groups.append(group);
        }
    }
    return groups;
}

bool GroupFilterComboBox::isAllSelected() const
{
    return m_selectedGroups.size() == m_groups.size();
}

void GroupFilterComboBox::rebuildMenu()
{
    m_menu.clear();

    QAction* selectAllAction = m_menu.addAction(tr("Select All"));
    connect(selectAllAction, &QAction::triggered, this, &GroupFilterComboBox::selectAll);

    QAction* clearAction = m_menu.addAction(tr("Clear"));
    connect(clearAction, &QAction::triggered, this, &GroupFilterComboBox::clearSelection);

    m_menu.addSeparator();

    for (const QString& group : m_groups) {
        auto* action = new QWidgetAction(&m_menu);
        auto* checkBox = new QCheckBox(group, &m_menu);
        const QColor color = colorForGroup(group);
        if (color.isValid()) {
            checkBox->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }").arg(color.name()));
        }
        checkBox->setChecked(m_selectedGroups.contains(group));
        action->setDefaultWidget(checkBox);
        m_menu.addAction(action);

        connect(checkBox, &QCheckBox::toggled, this, [this, group](bool checked) {
            if (checked) {
                m_selectedGroups.insert(group);
            }
            else {
                m_selectedGroups.remove(group);
            }
            updateButtonText();
            emitSelectionChanged();
        });
    }
}

QColor GroupFilterComboBox::colorForGroup(const QString& group) const
{
    const int index = static_cast<int>(m_groups.indexOf(group));
    if (index < 0 || index >= static_cast<int>(m_groupStyles.size())) {
        return {};
    }
    return m_groupStyles.at(index).groupColor;
}

void GroupFilterComboBox::selectAll()
{
    for (const QString& group : m_groups) {
        m_selectedGroups.insert(group);
    }
    updateButtonText();
    emitSelectionChanged();
    QTimer::singleShot(0, this, &GroupFilterComboBox::rebuildMenu);
}

void GroupFilterComboBox::clearSelection()
{
    m_selectedGroups.clear();
    updateButtonText();
    emitSelectionChanged();
    QTimer::singleShot(0, this, &GroupFilterComboBox::rebuildMenu);
}

void GroupFilterComboBox::emitSelectionChanged()
{
    emit selectedGroupsChanged(selectedGroups());
}

void GroupFilterComboBox::updateButtonText()
{
    if (m_groups.isEmpty() || m_selectedGroups.isEmpty()) {
        setText(tr("No groups"));
    }
    else if (isAllSelected()) {
        setText(tr("All groups"));
    }
    else if (m_selectedGroups.size() == 1) {
        setText(selectedGroups().first());
    }
    else {
        setText(tr("%n group(s)", nullptr, static_cast<int>(m_selectedGroups.size())));
    }
}
