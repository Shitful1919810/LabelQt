#include "core/UndoStack.h"

#include <QUndoCommand>

#include <utility>

namespace labelqt::core {

namespace {
class CallbackUndoCommand final : public QUndoCommand {
public:
    CallbackUndoCommand(QString text, QString undoText, QString redoText, std::function<void()> undo,
                        std::function<void()> redo)
        : QUndoCommand(std::move(text)), m_undoText(std::move(undoText)), m_redoText(std::move(redoText)),
          m_undo(std::move(undo)), m_redo(std::move(redo))
    {
    }

    QString undoText() const
    {
        return m_undoText.isEmpty() ? text() : m_undoText;
    }

    QString redoText() const
    {
        return m_redoText.isEmpty() ? text() : m_redoText;
    }

    void undo() override
    {
        if (m_undo) {
            m_undo();
        }
    }

    void redo() override
    {
        if (m_skipInitialRedo) {
            m_skipInitialRedo = false;
            return;
        }

        if (m_redo) {
            m_redo();
        }
    }

private:
    QString m_undoText;
    QString m_redoText;
    std::function<void()> m_undo;
    std::function<void()> m_redo;
    bool m_skipInitialRedo{true};
};
} // namespace

void UndoStack::push(Command command)
{
    push(std::move(command.text), std::move(command.undoText), std::move(command.redoText), std::move(command.undo),
         std::move(command.redo));
}

void UndoStack::push(QString text, std::function<void()> undo)
{
    push(std::move(text), std::move(undo), {});
}

void UndoStack::push(QString text, std::function<void()> undo, std::function<void()> redo)
{
    push(std::move(text), {}, {}, std::move(undo), std::move(redo));
}

void UndoStack::push(QString text, QString undoText, QString redoText, std::function<void()> undo,
                     std::function<void()> redo)
{
    if (undo || redo) {
        m_stack.push(new CallbackUndoCommand(std::move(text), std::move(undoText), std::move(redoText),
                                             std::move(undo), std::move(redo)));
        notifyChanged();
    }
}

void UndoStack::setChangedCallback(std::function<void()> callback)
{
    m_changedCallback = std::move(callback);
    notifyChanged();
}

bool UndoStack::canUndo() const noexcept
{
    return m_stack.canUndo();
}

bool UndoStack::canRedo() const noexcept
{
    return m_stack.canRedo();
}

QString UndoStack::undo()
{
    if (!m_stack.canUndo()) {
        notifyChanged();
        return {};
    }
    const auto* command = dynamic_cast<const CallbackUndoCommand*>(m_stack.command(m_stack.index() - 1));
    const QString message = command == nullptr ? QString() : command->undoText();
    m_stack.undo();
    notifyChanged();
    return message;
}

QString UndoStack::redo()
{
    if (!m_stack.canRedo()) {
        notifyChanged();
        return {};
    }
    const auto* command = dynamic_cast<const CallbackUndoCommand*>(m_stack.command(m_stack.index()));
    const QString message = command == nullptr ? QString() : command->redoText();
    m_stack.redo();
    notifyChanged();
    return message;
}

void UndoStack::clear()
{
    m_stack.clear();
    notifyChanged();
}

void UndoStack::notifyChanged()
{
    if (m_changedCallback) {
        m_changedCallback();
    }
}

} // namespace labelqt::core
