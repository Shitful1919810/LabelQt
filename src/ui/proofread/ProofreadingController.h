#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "core/UndoStack.h"
#include "services/project/SessionStateStore.h"
#include "services/proofread/ReviewRoundEditService.h"
#include "ui/proofread/ProofreadNotesWidget.h"

#include <QObject>

#include <functional>

class QAction;
class QLabel;
class QWidget;
class QMenu;
class LabelTableModel;

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
        std::function<QPair<int, int>()> currentSelection;
    };

    ProofreadingController(QWidget* parentWidget, labelqt::services::ProjectController& projectController,
                           labelqt::core::UndoStack& undoStack, Callbacks callbacks, QObject* parent = nullptr);

    void setUi(QAction* toggleAction, QAction* showChangesAction, QAction* compareAction, QLabel* statusLabel,
               QMenu* menu, ProofreadNotesWidget* notes, LabelTableModel* labelModel);
    void toggleBaseline();
    void showChanges();
    void compareWithProject();
    void updateUi(bool automationRunning);
    bool isActive() const;

private:
    bool loadRound();
    void clearRound();
    void editNote(ProofreadNotesWidget::Operation operation, QUuid id);
    bool commitRound(const std::optional<labelqt::services::ReviewRound>& round,
                     labelqt::services::ReviewEditToken token, const QString& description);
    void selectDialogResult(int imageIndex, int labelIndex);

    QWidget* m_parentWidget;
    labelqt::services::ProjectController& m_projectController;
    labelqt::core::UndoStack& m_undoStack;
    Callbacks m_callbacks;
    labelqt::services::SessionStateStore m_sessionStateStore;
    QAction* m_toggleAction{nullptr};
    QAction* m_showChangesAction{nullptr};
    QAction* m_compareAction{nullptr};
    QLabel* m_statusLabel{nullptr};
    bool m_automationRunning{false};
    QAction* m_modeAction{nullptr};
    QAction* m_clearAction{nullptr};
    ProofreadNotesWidget* m_notes{nullptr};
    LabelTableModel* m_labelModel{nullptr};
    std::optional<labelqt::services::ReviewRound> m_round;
    QStringList m_cachedComments;
    QString m_cachedPath;
    QUuid m_cachedDocumentSession;
    bool m_cacheValid{false};
    QString m_recordError;
};
