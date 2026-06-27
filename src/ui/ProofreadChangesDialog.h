#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/ReviewMetadataService.h"
#include "services/SessionStateStore.h"

#include <QDialog>
#include <QVector>

class ImageCanvas;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class ViewportFittedTableColumns;

namespace labelqt::services {
struct ProofreadReportTexts;
}

class ProofreadChangesDialog final : public QDialog {
    Q_OBJECT

public:
    ProofreadChangesDialog(const labelqt::core::Project& beforeProject, const labelqt::core::Project& currentProject,
                           labelqt::core::AppPreferences preferences,
                           labelqt::services::ReviewMetadata metadata,
                           QVector<labelqt::services::ReviewChange> changes, QWidget* parent = nullptr);
    ~ProofreadChangesDialog() override;

    int selectedImageIndex() const noexcept;
    int selectedLabelIndex() const noexcept;

public slots:
    void done(int result) override;

private:
    void buildUi();
    void populateRows();
    void restoreTableColumnWidths();
    void saveTableColumnWidths() const;
    void exportReport();
    int changeIndexForRow(int row) const;
    void scheduleDetailsUpdate(int row);
    void updateDetails(int row);
    void updatePreviewCanvases(const labelqt::services::ReviewChange& change);
    void updateJumpButton();
    void resizeTableRowsToContents();
    void resizeTableRowToContent(int row);
    QString changeKindText(labelqt::services::ReviewChangeKind kind) const;
    QString changeSummary(const labelqt::services::ReviewChange& change) const;
    QString diffHtml(const QString& beforeText, const QString& afterText) const;
    labelqt::services::ProofreadReportTexts reportTexts() const;
    void syncPreviewCanvases(ImageCanvas* sourceCanvas, int zoomPercent, QPointF normalizedCenter);

    const labelqt::core::Project& m_beforeProject;
    const labelqt::core::Project& m_currentProject;
    labelqt::core::AppPreferences m_preferences;
    labelqt::services::ReviewMetadata m_metadata;
    labelqt::services::SessionStateStore m_sessionStateStore;
    QVector<labelqt::services::ReviewChange> m_changes;
    QTableWidget* m_table{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QTextBrowser* m_diffBrowser{nullptr};
    ImageCanvas* m_beforeCanvas{nullptr};
    ImageCanvas* m_afterCanvas{nullptr};
    ViewportFittedTableColumns* m_tableColumns{nullptr};
    QPushButton* m_jumpButton{nullptr};
    bool m_isSyncingCanvasViews{false};
    bool m_isClosing{false};
    bool m_detailUpdateQueued{false};
    int m_pendingDetailRow{-1};
};
