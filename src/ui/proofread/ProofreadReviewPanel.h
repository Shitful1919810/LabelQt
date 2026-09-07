#pragma once
#include "core/UndoStack.h"
#include "services/proofread/ReviewSessionService.h"
#include <QWidget>
#include <functional>

class QComboBox;
class QAction;

class ProofreadReviewPanel final : public QWidget {
    Q_OBJECT
public:
    struct Callbacks {
        std::function<QVector<labelqt::services::ReviewChange>()> selected;
        std::function<QVector<labelqt::services::ReviewChange>()> allChanges;
        std::function<std::expected<void, QString>(const QVector<labelqt::services::ManualLabelPair>&)> validatePairs;
    };
    ProofreadReviewPanel(Callbacks callbacks, QWidget* parent = nullptr, bool allowManualPairs = true);
    ~ProofreadReviewPanel() override;
    void setSession(labelqt::services::ReviewSession session);
    const labelqt::services::ReviewSession& session() const;
    void setCurrentChange(const labelqt::services::ReviewChange* change);
    void setStatus(const QVector<labelqt::services::ReviewChange>& changes, labelqt::services::ReviewStatus status);
    void refreshSelection();
signals:
    void annotationsChanged();
    void pairsChanged();
    void nextUnseenRequested();

private:
    void changeState(labelqt::services::ReviewSession session, bool pairs);
    void applyState(labelqt::services::ReviewSession session, bool pairs);
    void refreshCurrentAnnotation();
    void pairSelected();
    void unpairSelected();
    Callbacks m_callbacks;
    labelqt::services::ReviewSession m_session;
    labelqt::core::UndoStack m_undo;
    QComboBox* m_status;
    QAction* m_pairAction;
    QAction* m_unpairAction;
    QString m_currentKey;
};
