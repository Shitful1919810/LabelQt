#include "ui/LabelEditDelegates.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int groupPopupDelayMs = 120;
constexpr int maximumPreviewTextLines = 4;
constexpr int textCellVerticalPadding = 8;

int textEditorHeightHint(QPlainTextEdit* editor)
{
    if (editor == nullptr) {
        return 0;
    }

    QTextDocument* document = editor->document();
    document->setTextWidth(std::max(1, editor->viewport()->width()));
    const int documentHeight = static_cast<int>(std::ceil(document->size().height()));
    const int explicitLineHeight = editor->fontMetrics().lineSpacing() * std::max(1, document->blockCount());
    return std::max(editor->fontMetrics().lineSpacing() + textCellVerticalPadding,
                    std::max(documentHeight, explicitLineHeight) + textCellVerticalPadding);
}
} // namespace

LabelTextDelegate::LabelTextDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void LabelTextDelegate::setCommitShortcut(QKeySequence shortcut)
{
    if (!shortcut.isEmpty()) {
        m_commitShortcut = std::move(shortcut);
    }
}

QWidget* LabelTextDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const
{
    auto* editor = new QPlainTextEdit(parent);
    editor->setFrameShape(QFrame::NoFrame);
    editor->setTabChangesFocus(true);
    editor->installEventFilter(const_cast<LabelTextDelegate*>(this));
    connect(editor, &QPlainTextEdit::textChanged, this,
            [delegate = const_cast<LabelTextDelegate*>(this), guardedEditor = QPointer<QPlainTextEdit>(editor),
             persistentIndex = QPersistentModelIndex(index)]() {
                if (guardedEditor != nullptr && persistentIndex.isValid()) {
                    emit delegate->editorHeightHintChanged(persistentIndex, guardedEditor,
                                                           textEditorHeightHint(guardedEditor));
                    emit delegate->editorTextChanged(persistentIndex, guardedEditor->toPlainText());
                }
            });
    connect(editor, &QObject::destroyed, this,
            [delegate = const_cast<LabelTextDelegate*>(this), persistentIndex = QPersistentModelIndex(index)]() {
                if (persistentIndex.isValid()) {
                    emit delegate->editorHeightHintChanged(persistentIndex, nullptr, 0);
                    emit delegate->editorTextPreviewFinished(persistentIndex);
                }
            });
    scheduleEditorHeightHint(editor, QPersistentModelIndex(index));
    return editor;
}

void LabelTextDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit == nullptr) {
        return;
    }

    textEdit->setPlainText(index.data(Qt::EditRole).toString());
    const QPointer<QPlainTextEdit> guardedEditor(textEdit);
    QTimer::singleShot(0, textEdit, [guardedEditor]() {
        if (guardedEditor != nullptr) {
            QTextCursor cursor = guardedEditor->textCursor();
            cursor.movePosition(QTextCursor::End);
            guardedEditor->setTextCursor(cursor);
        }
    });
    scheduleEditorHeightHint(textEdit, QPersistentModelIndex(index));
}

void LabelTextDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    auto* textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit == nullptr) {
        return;
    }

    model->setData(index, textEdit->toPlainText(), Qt::EditRole);
}

QSize LabelTextDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const int maximumHeight = option.fontMetrics.lineSpacing() * maximumPreviewTextLines + textCellVerticalPadding;
    size.setHeight(std::min(size.height(), maximumHeight));
    return size;
}

void LabelTextDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                             const QModelIndex& index) const
{
    editor->setGeometry(option.rect);
    scheduleEditorHeightHint(qobject_cast<QPlainTextEdit*>(editor), QPersistentModelIndex(index));
}

void LabelTextDelegate::scheduleEditorHeightHint(QPlainTextEdit* editor, QPersistentModelIndex index) const
{
    if (editor == nullptr || !index.isValid()) {
        return;
    }

    const auto* delegate = this;
    QTimer::singleShot(0, editor, [delegate, guardedEditor = QPointer<QPlainTextEdit>(editor), index]() {
        if (guardedEditor == nullptr || !index.isValid()) {
            return;
        }

        emit const_cast<LabelTextDelegate*>(delegate)->editorHeightHintChanged(index, guardedEditor,
                                                                               textEditorHeightHint(guardedEditor));
    });
}

bool LabelTextDelegate::eventFilter(QObject* object, QEvent* event)
{
    auto* editor = qobject_cast<QPlainTextEdit*>(object);
    if (editor == nullptr) {
        return QStyledItemDelegate::eventFilter(object, event);
    }

    if (event->type() == QEvent::FocusOut) {
        emit commitData(editor);
        emit closeEditor(editor);
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        const QKeySequence pressedKey(keyEvent->keyCombination());
        if (pressedKey.matches(m_commitShortcut) == QKeySequence::ExactMatch) {
            emit commitData(editor);
            emit closeEditor(editor);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
            return true;
        }
    }

    return QStyledItemDelegate::eventFilter(object, event);
}

LabelGroupDelegate::LabelGroupDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void LabelGroupDelegate::setGroups(QStringList groups, QVector<labelqt::core::LabelGroupStyle> groupStyles)
{
    m_groups = std::move(groups);
    m_groupStyles = std::move(groupStyles);
}

QWidget* LabelGroupDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const
{
    auto* comboBox = new QComboBox(parent);
    comboBox->addItems(m_groups);
    for (int i = 0; i < comboBox->count(); ++i) {
        const QColor color = colorForGroup(comboBox->itemText(i));
        if (color.isValid()) {
            comboBox->setItemData(i, color, Qt::ForegroundRole);
        }
    }
    connect(comboBox, &QComboBox::activated, this, [delegate = const_cast<LabelGroupDelegate*>(this), comboBox]() {
        emit delegate->commitData(comboBox);
        emit delegate->closeEditor(comboBox);
    });
    const QPointer<QComboBox> guardedComboBox(comboBox);
    QTimer::singleShot(groupPopupDelayMs, comboBox, [guardedComboBox]() {
        if (guardedComboBox != nullptr) {
            guardedComboBox->showPopup();
        }
    });
    return comboBox;
}

void LabelGroupDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* comboBox = qobject_cast<QComboBox*>(editor);
    if (comboBox == nullptr) {
        return;
    }

    comboBox->setCurrentText(index.data(Qt::EditRole).toString());
}

void LabelGroupDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    auto* comboBox = qobject_cast<QComboBox*>(editor);
    if (comboBox == nullptr) {
        return;
    }

    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

void LabelGroupDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);
    const QString text = itemOption.text;
    itemOption.text.clear();

    const QWidget* widget = itemOption.widget;
    QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, widget);

    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &itemOption, widget);
    const QString elidedText = itemOption.fontMetrics.elidedText(text, itemOption.textElideMode, textRect.width());
    const QColor groupColor = colorForGroup(index.data(Qt::DisplayRole).toString());
    const QPalette::ColorRole fallbackRole =
        itemOption.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;

    painter->save();
    painter->setFont(itemOption.font);
    painter->setPen(groupColor.isValid() ? groupColor : itemOption.palette.color(fallbackRole));
    painter->drawText(textRect, static_cast<int>(itemOption.displayAlignment), elidedText);
    painter->restore();
}

QColor LabelGroupDelegate::colorForGroup(const QString& group) const
{
    const int index = static_cast<int>(m_groups.indexOf(group));
    if (index < 0 || index >= static_cast<int>(m_groupStyles.size())) {
        return {};
    }
    return m_groupStyles.at(index).groupColor;
}
