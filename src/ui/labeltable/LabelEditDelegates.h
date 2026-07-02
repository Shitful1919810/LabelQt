#pragma once

#include "core/AppPreferences.h"

#include <QColor>
#include <QKeySequence>
#include <QPersistentModelIndex>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QVector>

class QPlainTextEdit;

class LabelTextDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit LabelTextDelegate(QObject* parent = nullptr);

    void setCommitShortcut(QKeySequence shortcut);
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

signals:
    void editorHeightHintChanged(QPersistentModelIndex index, QWidget* editor, int height);
    void editorTextChanged(QPersistentModelIndex index, QString text);
    void editorTextPreviewFinished(QPersistentModelIndex index);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void scheduleEditorHeightHint(QPlainTextEdit* editor, QPersistentModelIndex index) const;

    QKeySequence m_commitShortcut{QStringLiteral("Ctrl+Return")};
};

class LabelGroupDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit LabelGroupDelegate(QObject* parent = nullptr);

    void setGroups(QStringList groups, QVector<labelqt::core::LabelGroupStyle> groupStyles);
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    QColor colorForGroup(const QString& group) const;

    QStringList m_groups;
    QVector<labelqt::core::LabelGroupStyle> m_groupStyles;
};
