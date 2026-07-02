#pragma once

#include "core/AppPreferences.h"

#include <QColor>
#include <QMenu>
#include <QSet>
#include <QStringList>
#include <QToolButton>
#include <QVector>

class GroupFilterComboBox final : public QToolButton {
    Q_OBJECT

public:
    explicit GroupFilterComboBox(QWidget* parent = nullptr);

    void setGroups(const QStringList& groups, QVector<labelqt::core::LabelGroupStyle> groupStyles = {});
    QStringList selectedGroups() const;
    bool isAllSelected() const;

signals:
    void selectedGroupsChanged(const QStringList& groups);

private:
    void rebuildMenu();
    void refreshMenuChecks();
    void selectAll();
    void clearSelection();
    void emitSelectionChanged();
    void updateButtonText();
    QColor colorForGroup(const QString& group) const;

    QStringList m_groups;
    QVector<labelqt::core::LabelGroupStyle> m_groupStyles;
    QSet<QString> m_selectedGroups;
    QMenu m_menu;
};
