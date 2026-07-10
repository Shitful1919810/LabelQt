#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "core/UndoStack.h"
#include "services/project/SessionStateStore.h"

#include <QObject>

#include <functional>

class QAction;
class QLabel;
class QWidget;

class ProofreadingController final : public QObject {
    Q_OBJECT

public:
    struct Callbacks {
        std::function<labelqt::core::Project&()> project;
        std::function<const labelqt::core::AppPreferences&()> preferences;
        std::function<void()> commitActiveTextInput;
        std::function<bool()> isEditingBlocked;
        std::function<void()> markDirty;
        std::function<void(int imageIndex, int labelIndex)> selectResult;
        std::function<void(const QString& message, int timeoutMs)> showStatus;
    };

    ProofreadingController(QWidget* parentWidget, labelqt::core::UndoStack& undoStack, Callbacks callbacks,
                           QObject* parent = nullptr);

    void setUi(QAction* toggleAction, QAction* showChangesAction, QAction* compareAction, QLabel* statusLabel);
    void toggleBaseline();
    void showChanges();
    void compareWithProject();
    void updateUi(bool automationRunning);
    bool isActive() const;

private:
    void endBaseline();
    void selectDialogResult(int imageIndex, int labelIndex);

    QWidget* m_parentWidget;
    labelqt::core::UndoStack& m_undoStack;
    Callbacks m_callbacks;
    labelqt::services::SessionStateStore m_sessionStateStore;
    QAction* m_toggleAction{nullptr};
    QAction* m_showChangesAction{nullptr};
    QAction* m_compareAction{nullptr};
    QLabel* m_statusLabel{nullptr};
    bool m_automationRunning{false};
};
