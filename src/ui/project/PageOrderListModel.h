#pragma once

#include "core/Project.h"
#include "core/UndoStack.h"
#include "services/project/PageManagementDraft.h"
#include "services/project/ProjectImageRenameService.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>

class PageOrderListModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ImageNameColumn = 0,
        RenamePreviewColumn,
        RenameStatusColumn,
        OriginalIndexColumn,
        ColumnCount,
    };

    explicit PageOrderListModel(const labelqt::core::Project& project, QObject* parent = nullptr);

    QVector<int> pageOrder() const;
    QVector<int> lastMovedSourceIndexes() const;
    int sourceIndexForRow(int row) const;
    int rowForSourceIndex(int sourceIndex) const;
    void refreshAllRows();
    void setRenamePlan(const labelqt::services::ProjectImageRenamePlan& plan);
    void setPendingRenamePlan(const labelqt::services::ProjectImageRenamePlan& plan);
    void clearPendingRenamePlan();
    void clearPendingSourceIndexes(const QVector<int>& indexes);
    const labelqt::services::PageManagementDraft& draft() const;
    labelqt::core::UndoStack& undoStack();
    void moveSourceIndexes(QVector<int> sourceIndexes, int delta);
    void removeSourceIndexes(QVector<int> sourceIndexes);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

signals:
    void draftChanged();

private:
    void resetOrder(QVector<int> order, QVector<int> selectedSourceIndexes);
    void restoreDraft(labelqt::services::PageManagementDraft::State state);
    void rememberChange(labelqt::services::PageManagementDraft::State before);

    labelqt::services::PageManagementDraft m_draft;
    labelqt::core::UndoStack m_undo;
    QVector<int> m_lastMovedSourceIndexes;
    QHash<int, labelqt::services::ProjectImageRenameItem> m_renameItemsBySourceIndex;
};
