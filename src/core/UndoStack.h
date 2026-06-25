#pragma once

#include <QString>
#include <QUndoStack>

#include <functional>

namespace labelqt::core {

class UndoStack {
public:
    struct Command {
        QString text;
        QString undoText;
        QString redoText;
        std::function<void()> undo;
        std::function<void()> redo;
    };

    void push(Command command);
    void push(QString text, std::function<void()> undo);
    void push(QString text, std::function<void()> undo, std::function<void()> redo);
    void push(QString text, QString undoText, QString redoText, std::function<void()> undo,
              std::function<void()> redo);
    void setChangedCallback(std::function<void()> callback);
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    QString undo();
    QString redo();
    void clear();

private:
    void notifyChanged();

    QUndoStack m_stack;
    std::function<void()> m_changedCallback;
};

} // namespace labelqt::core
