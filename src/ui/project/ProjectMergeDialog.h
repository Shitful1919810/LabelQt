#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/project/ProjectMergeService.h"

#include <QDialog>
#include <QPointF>
#include <QPointer>
#include <QVector>
#include <array>

class MergeCandidatePreview;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;

class ProjectMergeDialog final : public QDialog {
    Q_OBJECT

public:
    ProjectMergeDialog(labelqt::services::ProjectMergePlan mergePlan, labelqt::core::AppPreferences preferences,
                       QWidget* parent = nullptr);
    ~ProjectMergeDialog() override;

    QVector<int> selectedCandidateIndexes() const;
    bool shouldOpenMergedProjectAfterSave() const noexcept;
    void setShouldOpenMergedProjectAfterSave(bool shouldOpen);

public slots:
    void done(int result) override;

private:
    void buildUi();
    void populateConflictList();
    void displayConflict(int conflictIndex);
    void displayCandidate(std::size_t slot, int candidateIndex);
    void updateSummary();
    void selectCandidate(int conflictIndex, int candidateIndex);
    void syncCandidateCanvasViews(std::size_t sourceSlot, int zoomPercent, QPointF normalizedCenter);
    void stopPreviews();

    struct ConflictViewState {
        std::array<int, 2> candidates{-1, -1};
        int zoomPercent{0};
        QPointF normalizedCenter{0.5, 0.5};
    };

    labelqt::services::ProjectMergePlan m_mergePlan;
    labelqt::core::AppPreferences m_preferences;
    QVector<int> m_selectedCandidateIndexes;
    QVector<ConflictViewState> m_conflictViewStates;
    QLabel* m_summaryLabel{nullptr};
    QCheckBox* m_openMergedProjectCheckBox{nullptr};
    QListWidget* m_conflictList{nullptr};
    QLabel* m_conflictTitle{nullptr};
    QComboBox* m_sourceChoice{nullptr};
    std::array<QComboBox*, 2> m_candidateChoices{};
    std::array<QPointer<MergeCandidatePreview>, 2> m_previews;
    int m_currentConflict{-1};
    bool m_isUpdating{false};
    bool m_isClosing{false};
};
