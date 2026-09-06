#include "ui/project/PageOrderListModel.h"

#include <QCoreApplication>
#include <QColor>
#include <QDataStream>
#include <QFileInfo>
#include <QIODevice>
#include <QMimeData>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
const char* pageOrderMimeType = "application/x-labelqt-page-order-rows";
}

PageOrderListModel::PageOrderListModel(const labelqt::core::Project& project, QObject* parent)
    : QAbstractTableModel(parent), m_draft(project)
{
}

QVector<int> PageOrderListModel::pageOrder() const
{
    return m_draft.order();
}

QVector<int> PageOrderListModel::lastMovedSourceIndexes() const
{
    return m_lastMovedSourceIndexes;
}

int PageOrderListModel::sourceIndexForRow(int row) const
{
    if (row < 0 || row >= m_draft.order().size()) {
        return -1;
    }
    return m_draft.order().at(row);
}

int PageOrderListModel::rowForSourceIndex(int sourceIndex) const
{
    return static_cast<int>(m_draft.order().indexOf(sourceIndex));
}

void PageOrderListModel::refreshAllRows()
{
    if (m_draft.order().isEmpty()) {
        return;
    }

    emit dataChanged(index(0, 0), index(static_cast<int>(m_draft.order().size()) - 1, ColumnCount - 1),
                     {Qt::DisplayRole, Qt::ToolTipRole});
}

void PageOrderListModel::setRenamePlan(const labelqt::services::ProjectImageRenamePlan& plan)
{
    m_renameItemsBySourceIndex.clear();
    for (const labelqt::services::ProjectImageRenameItem& item : plan.items) {
        if (item.imageIndex >= 0) {
            m_renameItemsBySourceIndex.insert(item.imageIndex, item);
        }
    }
    refreshAllRows();
}

void PageOrderListModel::setPendingRenamePlan(const labelqt::services::ProjectImageRenamePlan& plan)
{
    const auto before = m_draft.state();
    if (m_draft.queue(plan))
        rememberChange(before);
    refreshAllRows();
    if (before != m_draft.state())
        emit draftChanged();
}

void PageOrderListModel::clearPendingRenamePlan()
{
    clearPendingSourceIndexes(m_draft.order());
}

void PageOrderListModel::clearPendingSourceIndexes(const QVector<int>& indexes)
{
    const auto before = m_draft.state();
    m_draft.clearPending(indexes);
    rememberChange(before);
    refreshAllRows();
    if (before != m_draft.state())
        emit draftChanged();
}

const labelqt::services::PageManagementDraft& PageOrderListModel::draft() const
{
    return m_draft;
}
labelqt::core::UndoStack& PageOrderListModel::undoStack()
{
    return m_undo;
}

void PageOrderListModel::restoreDraft(labelqt::services::PageManagementDraft::State state)
{
    beginResetModel();
    m_draft.restore(std::move(state));
    endResetModel();
    emit draftChanged();
}

void PageOrderListModel::rememberChange(labelqt::services::PageManagementDraft::State before)
{
    const auto after = m_draft.state();
    if (before == after)
        return;
    m_undo.push(
        QCoreApplication::translate("PageOrderListModel", "Edit page draft"), [this, before] { restoreDraft(before); },
        [this, after] { restoreDraft(after); });
}

void PageOrderListModel::moveSourceIndexes(QVector<int> sourceIndexes, int delta)
{
    if (delta == 0 || sourceIndexes.isEmpty()) {
        return;
    }

    QSet<int> selectedSourceIndexes;
    selectedSourceIndexes.reserve(sourceIndexes.size());
    for (int sourceIndex : std::as_const(sourceIndexes)) {
        if (m_draft.order().contains(sourceIndex)) {
            selectedSourceIndexes.insert(sourceIndex);
        }
    }
    if (selectedSourceIndexes.isEmpty()) {
        return;
    }

    QVector<int> reordered = m_draft.order();
    if (delta < 0) {
        for (int i = 1; i < reordered.size(); ++i) {
            if (selectedSourceIndexes.contains(reordered.at(i)) &&
                !selectedSourceIndexes.contains(reordered.at(i - 1))) {
                std::swap(reordered[i], reordered[i - 1]);
            }
        }
    }
    else {
        for (int i = static_cast<int>(reordered.size()) - 2; i >= 0; --i) {
            if (selectedSourceIndexes.contains(reordered.at(i)) &&
                !selectedSourceIndexes.contains(reordered.at(i + 1))) {
                std::swap(reordered[i], reordered[i + 1]);
            }
        }
    }

    resetOrder(std::move(reordered), sourceIndexes);
}

void PageOrderListModel::removeSourceIndexes(QVector<int> sourceIndexes)
{
    if (sourceIndexes.isEmpty()) {
        return;
    }

    QSet<int> selectedSourceIndexes;
    selectedSourceIndexes.reserve(sourceIndexes.size());
    for (int sourceIndex : std::as_const(sourceIndexes)) {
        selectedSourceIndexes.insert(sourceIndex);
    }

    QVector<int> reordered;
    reordered.reserve(m_draft.order().size());
    for (int sourceIndex : std::as_const(m_draft.order())) {
        if (!selectedSourceIndexes.contains(sourceIndex)) {
            reordered.append(sourceIndex);
        }
    }

    QVector<int> nextSelection;
    if (!reordered.isEmpty()) {
        int firstRemovedRow = static_cast<int>(m_draft.order().size());
        for (int sourceIndex : std::as_const(sourceIndexes)) {
            const int row = rowForSourceIndex(sourceIndex);
            if (row >= 0) {
                firstRemovedRow = std::min(firstRemovedRow, row);
            }
        }
        nextSelection.append(reordered.at(std::clamp(firstRemovedRow, 0, static_cast<int>(reordered.size()) - 1)));
    }
    resetOrder(std::move(reordered), nextSelection);
}

int PageOrderListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_draft.order().size());
}

int PageOrderListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PageOrderListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_draft.order().size() || index.column() < 0 ||
        index.column() >= ColumnCount) {
        return {};
    }

    const int sourceIndex = m_draft.order().at(index.row());
    if (sourceIndex < 0 || sourceIndex >= m_draft.project().images().size()) {
        return {};
    }

    const labelqt::core::ImageEntry& image = m_draft.project().images().at(sourceIndex);
    const QString pageName = image.name.isEmpty() ? image.path : image.name;
    const labelqt::services::ProjectImageRenameItem renameItem = m_renameItemsBySourceIndex.value(sourceIndex);
    const labelqt::services::ProjectImageRenameItem pendingRenameItem = m_draft.pendingItem(sourceIndex);
    const bool imageMissing = !QFileInfo::exists(image.path);
    switch (role) {
    case Qt::DisplayRole:
        if (index.column() == ImageNameColumn) {
            return pageName;
        }
        if (index.column() == RenamePreviewColumn) {
            return pendingRenameItem.included ? pendingRenameItem.newName : QString();
        }
        if (index.column() == RenameStatusColumn) {
            if (!renameItem.included) {
                return imageMissing ? QCoreApplication::translate("PageOrderListModel", "Image missing")
                                    : QCoreApplication::translate("PageOrderListModel", "Not selected");
            }
            if (!renameItem.error.isEmpty()) {
                return renameItem.error;
            }
            if (renameItem.referenceOnly) {
                return renameItem.isChanged()
                           ? QCoreApplication::translate("PageOrderListModel", "Will point to %1").arg(renameItem.newName)
                           : QCoreApplication::translate("PageOrderListModel", "Unchanged");
            }
            return renameItem.isChanged()
                       ? QCoreApplication::translate("PageOrderListModel", "Will be renamed to %1").arg(renameItem.newName)
                       : QCoreApplication::translate("PageOrderListModel", "Unchanged");
        }
        return QString::number(sourceIndex + 1).rightJustified(3, QLatin1Char('0'));
    case Qt::ToolTipRole:
        if (index.column() == RenamePreviewColumn) {
            return pendingRenameItem.included ? pendingRenameItem.newName : QString();
        }
        if (index.column() == RenameStatusColumn) {
            return data(index, Qt::DisplayRole);
        }
        return pageName;
    case Qt::ForegroundRole:
        if (index.column() == RenameStatusColumn) {
            if ((renameItem.included && !renameItem.error.isEmpty()) || (!renameItem.included && imageMissing)) {
                return QColor(Qt::red);
            }
        }
        return {};
    case Qt::UserRole:
        return sourceIndex;
    default:
        return {};
    }
}

QVariant PageOrderListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case ImageNameColumn:
        return QCoreApplication::translate("PageOrderListModel", "Current image");
    case RenamePreviewColumn:
        return QCoreApplication::translate("PageOrderListModel", "Pending target");
    case RenameStatusColumn:
        return QCoreApplication::translate("PageOrderListModel", "Input preview");
    case OriginalIndexColumn:
        return QCoreApplication::translate("PageOrderListModel", "Original #");
    default:
        return {};
    }
}

Qt::ItemFlags PageOrderListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled;
}

QStringList PageOrderListModel::mimeTypes() const
{
    return {QString::fromLatin1(pageOrderMimeType)};
}

QMimeData* PageOrderListModel::mimeData(const QModelIndexList& indexes) const
{
    QVector<int> rows;
    rows.reserve(indexes.size());
    for (const QModelIndex& index : indexes) {
        if (index.isValid() && index.column() == 0 && !rows.contains(index.row())) {
            rows.append(index.row());
        }
    }
    std::sort(rows.begin(), rows.end());

    auto* mimeData = new QMimeData;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << rows;
    mimeData->setData(QString::fromLatin1(pageOrderMimeType), payload);
    return mimeData;
}

bool PageOrderListModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                      const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (action != Qt::MoveAction || column > 0 || data == nullptr ||
        !data->hasFormat(QString::fromLatin1(pageOrderMimeType))) {
        return false;
    }

    QVector<int> sourceRows;
    QByteArray payload = data->data(QString::fromLatin1(pageOrderMimeType));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream >> sourceRows;
    std::sort(sourceRows.begin(), sourceRows.end());
    sourceRows.erase(std::unique(sourceRows.begin(), sourceRows.end()), sourceRows.end());
    sourceRows.erase(
        std::remove_if(sourceRows.begin(), sourceRows.end(),
                       [this](int sourceRow) { return sourceRow < 0 || sourceRow >= m_draft.order().size(); }),
        sourceRows.end());
    if (sourceRows.isEmpty()) {
        return false;
    }

    int destinationRow = row;
    if (destinationRow < 0 && parent.isValid()) {
        destinationRow = parent.row();
    }
    if (destinationRow < 0) {
        destinationRow = static_cast<int>(m_draft.order().size());
    }
    destinationRow = std::clamp(destinationRow, 0, static_cast<int>(m_draft.order().size()));

    QVector<int> movingPages;
    movingPages.reserve(sourceRows.size());
    QSet<int> sourceRowSet;
    sourceRowSet.reserve(sourceRows.size());
    for (int sourceRow : sourceRows) {
        movingPages.append(m_draft.order().at(sourceRow));
        sourceRowSet.insert(sourceRow);
    }

    int adjustedDestinationRow = destinationRow;
    for (int sourceRow : sourceRows) {
        if (sourceRow < destinationRow) {
            --adjustedDestinationRow;
        }
    }

    QVector<int> reordered;
    reordered.reserve(m_draft.order().size());
    for (int i = 0; i < m_draft.order().size(); ++i) {
        if (!sourceRowSet.contains(i)) {
            reordered.append(m_draft.order().at(i));
        }
    }

    adjustedDestinationRow = std::clamp(adjustedDestinationRow, 0, static_cast<int>(reordered.size()));
    for (int i = 0; i < movingPages.size(); ++i) {
        reordered.insert(adjustedDestinationRow + i, movingPages.at(i));
    }

    if (reordered == m_draft.order()) {
        return false;
    }

    resetOrder(std::move(reordered), movingPages);
    return true;
}

Qt::DropActions PageOrderListModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

void PageOrderListModel::resetOrder(QVector<int> order, QVector<int> selectedSourceIndexes)
{
    if (order == m_draft.order()) {
        return;
    }

    m_lastMovedSourceIndexes = std::move(selectedSourceIndexes);
    beginResetModel();
    const auto before = m_draft.state();
    m_draft.setOrder(std::move(order));
    endResetModel();
    rememberChange(before);
    emit draftChanged();
}
