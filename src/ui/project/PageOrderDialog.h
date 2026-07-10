#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/project/ProjectImageRenameService.h"
#include "services/project/SessionStateStore.h"

#include <QDialog>
#include <QVector>

#include <functional>

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
    void setPendingImageRenameHandler(
        std::function<bool(const labelqt::services::ProjectImageRenamePlan&)> handler);

public slots:
    void done(int result) override;

private:
    void buildUi();
    void acceptRequested();
    void updatePreview(const QModelIndex& currentIndex);
    labelqt::core::ImageEntry previewImageForSourceIndex(int sourceIndex) const;
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
    labelqt::services::ProjectImageRenamePlan m_visibleImageRenamePlan;
    std::function<bool(const labelqt::services::ProjectImageRenamePlan&)> m_pendingImageRenameHandler;
    bool m_hasPendingImageRenamePlan{false};
};
