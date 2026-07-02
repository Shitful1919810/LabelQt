#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/project/ProjectImageRenameService.h"
#include "services/project/SessionStateStore.h"

#include <QDialog>
#include <QVector>

class ImageCanvas;
class QTableView;
class QModelIndex;
class PageOrderListModel;
class QToolButton;
class ViewportFittedTableColumns;
class ImageRenamePreviewWidget;

class PageOrderDialog final : public QDialog {
    Q_OBJECT

public:
    PageOrderDialog(const labelqt::core::Project& project, labelqt::core::AppPreferences preferences,
                    QWidget* parent = nullptr, bool enableImageRename = false);

    QVector<int> pageOrder() const;
    bool hasPendingImageRenamePlan() const noexcept;
    labelqt::services::ProjectImageRenamePlan pendingImageRenamePlan() const;

public slots:
    void done(int result) override;

private:
    void buildUi();
    void updatePreview(const QModelIndex& currentIndex);
    void updateImageRenamePreview();
    void updatePageActionButtons();
    void restoreTableColumnWidths();
    void saveTableColumnWidths() const;
    QVector<int> selectedPageSourceIndexes() const;
    QVector<int> renameTargetSourceIndexes() const;

    const labelqt::core::Project& m_project;
    labelqt::core::AppPreferences m_preferences;
    bool m_enableImageRename{false};
    labelqt::services::SessionStateStore m_sessionStateStore;
    ImageCanvas* m_canvas{nullptr};
    QTableView* m_pageTable{nullptr};
    ViewportFittedTableColumns* m_pageTableColumns{nullptr};
    PageOrderListModel* m_pageOrderModel{nullptr};
    QToolButton* m_movePageUpButton{nullptr};
    QToolButton* m_movePageDownButton{nullptr};
    QToolButton* m_removePageButton{nullptr};
    ImageRenamePreviewWidget* m_imageRenameWidget{nullptr};
    labelqt::services::ProjectImageRenamePlan m_pendingImageRenamePlan;
    bool m_hasPendingImageRenamePlan{false};
};
