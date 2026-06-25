#pragma once

#include "core/AppPreferences.h"
#include "core/Project.h"
#include "services/SessionStateStore.h"

#include <QDialog>
#include <QVector>

class ImageCanvas;
class QTableView;
class QModelIndex;
class PageOrderListModel;
class QToolButton;
class ViewportFittedTableColumns;

class PageOrderDialog final : public QDialog {
    Q_OBJECT

public:
    PageOrderDialog(const labelqt::core::Project& project, labelqt::core::AppPreferences preferences,
                    QWidget* parent = nullptr);

    QVector<int> pageOrder() const;

public slots:
    void done(int result) override;

private:
    void buildUi();
    void updatePreview(const QModelIndex& currentIndex);
    void updatePageActionButtons();
    void restoreTableColumnWidths();
    void saveTableColumnWidths() const;
    QVector<int> selectedPageSourceIndexes() const;

    const labelqt::core::Project& m_project;
    labelqt::core::AppPreferences m_preferences;
    labelqt::services::SessionStateStore m_sessionStateStore;
    ImageCanvas* m_canvas{nullptr};
    QTableView* m_pageTable{nullptr};
    ViewportFittedTableColumns* m_pageTableColumns{nullptr};
    PageOrderListModel* m_pageOrderModel{nullptr};
    QToolButton* m_movePageUpButton{nullptr};
    QToolButton* m_movePageDownButton{nullptr};
    QToolButton* m_removePageButton{nullptr};
};
