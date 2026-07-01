#include "ui/CheckableFilterButton.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWidgetAction>

#include <algorithm>
#include <utility>

CheckableFilterButton::CheckableFilterButton(QWidget* parent) : QToolButton(parent)
{
    setPopupMode(QToolButton::InstantPopup);
    setMenu(new QMenu(this));
    setTexts(QString(), QString(), QStringLiteral("%1"));
}

void CheckableFilterButton::setTexts(QString allText, QString noneText, QString countText)
{
    m_allText = std::move(allText);
    m_noneText = std::move(noneText);
    m_countText = std::move(countText);
    updateButtonText();
}

void CheckableFilterButton::setOptions(QVector<CheckableFilterOption> options)
{
    m_options = std::move(options);
    m_selectedKeys.clear();
    for (const CheckableFilterOption& option : m_options) {
        m_selectedKeys.insert(option.key);
    }
    rebuildMenu();
    updateButtonText();
}

QSet<QString> CheckableFilterButton::selectedKeys() const
{
    return m_selectedKeys;
}

QStringList CheckableFilterButton::selectedTexts() const
{
    QStringList texts;
    texts.reserve(m_selectedKeys.size());
    for (const CheckableFilterOption& option : m_options) {
        if (m_selectedKeys.contains(option.key)) {
            texts.append(option.text);
        }
    }
    return texts;
}

bool CheckableFilterButton::isAllSelected() const
{
    return m_selectedKeys.size() == m_options.size();
}

void CheckableFilterButton::rebuildMenu()
{
    QMenu* currentMenu = menu();
    if (currentMenu == nullptr) {
        return;
    }

    currentMenu->clear();
    auto* actionsWidgetAction = new QWidgetAction(currentMenu);
    auto* actionsWidget = new QWidget(currentMenu);
    auto* actionsLayout = new QHBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(8, 2, 8, 2);
    actionsLayout->setSpacing(4);
    auto* selectAllButton = new QToolButton(actionsWidget);
    selectAllButton->setText(tr("Select All"));
    selectAllButton->setAutoRaise(true);
    selectAllButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* separator = new QFrame(actionsWidget);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    auto* clearButton = new QToolButton(actionsWidget);
    clearButton->setText(tr("Clear"));
    clearButton->setAutoRaise(true);
    clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    actionsLayout->addWidget(selectAllButton);
    actionsLayout->addWidget(separator);
    actionsLayout->addWidget(clearButton);
    actionsLayout->addStretch();
    actionsWidgetAction->setDefaultWidget(actionsWidget);
    currentMenu->addAction(actionsWidgetAction);
    connect(selectAllButton, &QToolButton::clicked, this, &CheckableFilterButton::selectAll);
    connect(clearButton, &QToolButton::clicked, this, &CheckableFilterButton::clearSelection);
    currentMenu->addSeparator();

    for (const CheckableFilterOption& option : m_options) {
        auto* action = new QWidgetAction(currentMenu);
        auto* checkBox = new QCheckBox(option.text, currentMenu);
        checkBox->setProperty("filterKey", option.key);
        checkBox->setChecked(m_selectedKeys.contains(option.key));
        action->setDefaultWidget(checkBox);
        currentMenu->addAction(action);

        connect(checkBox, &QCheckBox::toggled, this, [this, key = option.key](bool checked) {
            if (checked) {
                m_selectedKeys.insert(key);
            } else {
                m_selectedKeys.remove(key);
            }
            updateButtonText();
            emit selectionChanged();
        });
    }
}

void CheckableFilterButton::refreshMenuChecks()
{
    QMenu* currentMenu = menu();
    if (currentMenu == nullptr) {
        return;
    }

    for (QAction* action : currentMenu->actions()) {
        auto* widgetAction = qobject_cast<QWidgetAction*>(action);
        if (widgetAction == nullptr) {
            continue;
        }
        auto* checkBox = qobject_cast<QCheckBox*>(widgetAction->defaultWidget());
        if (checkBox == nullptr) {
            continue;
        }
        const QString key = checkBox->property("filterKey").toString();
        const QSignalBlocker blocker(checkBox);
        checkBox->setChecked(m_selectedKeys.contains(key));
    }
}

void CheckableFilterButton::selectAll()
{
    m_selectedKeys.clear();
    for (const CheckableFilterOption& option : m_options) {
        m_selectedKeys.insert(option.key);
    }
    refreshMenuChecks();
    updateButtonText();
    emit selectionChanged();
}

void CheckableFilterButton::clearSelection()
{
    m_selectedKeys.clear();
    refreshMenuChecks();
    updateButtonText();
    emit selectionChanged();
}

void CheckableFilterButton::updateButtonText()
{
    if (m_options.isEmpty() || m_selectedKeys.isEmpty()) {
        setText(m_noneText);
    } else if (isAllSelected()) {
        setText(m_allText);
    } else if (m_selectedKeys.size() == 1) {
        const auto it = std::ranges::find_if(m_options, [this](const CheckableFilterOption& option) {
            return m_selectedKeys.contains(option.key);
        });
        setText(it == m_options.cend() ? m_noneText : it->text);
    } else {
        setText(m_countText.arg(m_selectedKeys.size()));
    }
}
