#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/project/SessionStateStore.h"
#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ReviewChangeClassifier.h"
#include "services/proofread/ReviewMetadataService.h"

#include <QDialog>
#include <QVector>

class ImageCanvas;
class CheckableFilterButton;
class QLabel;
class QPushButton;
class QTableView;
class QTextBrowser;
class ViewportFittedTableColumns;
class ProofreadChangeFilterModel;
class ProofreadChangeTableModel;
class ProofreadReviewPanel;
class QComboBox;

namespace labelqt::services {
struct ProofreadReportTexts;
}

struct ProofreadChangesDialogLabels {
    QString leftProjectTitle;
    QString rightProjectTitle;
    bool jumpToLeftProject{false};
    bool reviewEnabled{false};
    bool projectReviewSession{false};
    bool roundReview{false};
};

class ProofreadChangesDialog final : public QDialog {
    Q_OBJECT

public:
    ProofreadChangesDialog(const labelqt::core::Project& beforeProject, const labelqt::core::Project& currentProject,
                           labelqt::core::AppPreferences preferences,
                           labelqt::services::ProjectComparisonPlan comparison,
                           QVector<labelqt::services::ReviewChange> changes, ProofreadChangesDialogLabels labels = {},
                           QWidget* parent = nullptr);
    ~ProofreadChangesDialog() override;

    int selectedImageIndex() const noexcept;
    int selectedLabelIndex() const noexcept;
    QJsonObject reviewSession() const;
    const labelqt::services::ReviewChange* selectedChange() const;

public slots:
    void done(int result) override;

private:
    void buildUi();
    void rebuildFilters();
    void applyFilters();
    void restoreTableColumnWidths();
    void saveTableColumnWidths() const;
    void exportReport(bool pdf);
    int changeIndexForRow(int row) const;
    void scheduleDetailsUpdate(int row);
    void updateDetails(int row);
    void updatePreviewCanvases(const labelqt::services::ReviewChange& change);
    void updateJumpButton();
    void resizeTableRowsToContents();
    void resizeTableRowToContent(int row);
    QString facetText(labelqt::services::ReviewChangeFacet facet) const;
    QVector<labelqt::services::ReviewChange> filteredChanges() const;
    QString reportFilterDescription() const;
    labelqt::services::ProofreadReportTexts reportTexts() const;
    void syncPreviewCanvases(ImageCanvas* sourceCanvas, int zoomPercent, QPointF normalizedCenter);
    void updateReviewAnnotations();
    void updateManualPairs();
    QVector<labelqt::services::ReviewChange> selectedChanges() const;

    const labelqt::core::Project& m_beforeProject;
    const labelqt::core::Project& m_currentProject;
    labelqt::core::AppPreferences m_preferences;
    labelqt::services::ReviewMetadata m_metadata;
    labelqt::services::SessionStateStore m_sessionStateStore;
    ProofreadChangesDialogLabels m_labels;
    QTableView* m_table{nullptr};
    ProofreadChangeTableModel* m_tableModel{nullptr};
    ProofreadChangeFilterModel* m_filterModel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    CheckableFilterButton* m_pageFilterButton{nullptr};
    CheckableFilterButton* m_kindFilterButton{nullptr};
    CheckableFilterButton* m_summaryFilterButton{nullptr};
    QTextBrowser* m_diffBrowser{nullptr};
    ImageCanvas* m_beforeCanvas{nullptr};
    ImageCanvas* m_afterCanvas{nullptr};
    ViewportFittedTableColumns* m_tableColumns{nullptr};
    QPushButton* m_jumpButton{nullptr};
    bool m_isSyncingCanvasViews{false};
    bool m_isClosing{false};
    bool m_detailUpdateQueued{false};
    int m_pendingDetailRow{-1};
    ProofreadReviewPanel* m_reviewPanel{nullptr};
    QComboBox* m_reviewFilter{nullptr};
    QVector<labelqt::services::ProjectComparisonPagePair> m_pagePairs;
    QString m_sessionKey;
    bool m_sessionWritable{true};
    QJsonObject m_initialReviewSession;
};
