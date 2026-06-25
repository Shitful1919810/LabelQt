#include "ui/PageOrderDialog.h"

#include "ui/DialogWindowUtils.h"
#include "ui/ImageCanvas.h"
#include "ui/PageOrderListModel.h"
#include "ui/ViewportFittedTableColumns.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QLabel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int defaultPageOrderImageNameColumnWidth = 220;
constexpr int defaultPageOrderOriginalIndexColumnWidth = 80;
constexpr int minimumPageOrderImageNameColumnWidth = 160;
constexpr int minimumPageOrderOriginalIndexColumnWidth = 60;
} // namespace

PageOrderDialog::PageOrderDialog(const labelqt::core::Project& project, labelqt::core::AppPreferences preferences,
                                 QWidget* parent)
    : QDialog(parent), m_project(project), m_preferences(std::move(preferences))
{
    buildUi();
}

QVector<int> PageOrderDialog::pageOrder() const
{
    return m_pageOrderModel == nullptr ? QVector<int>{} : m_pageOrderModel->pageOrder();
}

void PageOrderDialog::done(int result)
{
    saveTableColumnWidths();
    QDialog::done(result);
}

void PageOrderDialog::buildUi()
{
    setWindowTitle(tr("Reorder Pages"));
    labelqt::ui::configureLargeDialogWindow(*this, QSize(1100, 760));

    auto* rootLayout = new QVBoxLayout(this);
    auto* hintLabel = new QLabel(tr("Drag pages in the list to change their order."), this);
    hintLabel->setWordWrap(true);
    rootLayout->addWidget(hintLabel);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_canvas = new ImageCanvas(splitter);
    m_canvas->setReadOnly(true);
    m_canvas->setPreferences(m_preferences);
    m_canvas->setGroups(m_project.groups());
    m_canvas->setVisibleGroups(m_project.groups());
    splitter->addWidget(m_canvas);

    auto* pageListPanel = new QWidget(splitter);
    auto* pageListLayout = new QVBoxLayout(pageListPanel);
    pageListLayout->setContentsMargins(0, 0, 0, 0);
    pageListLayout->setSpacing(6);

    auto* pageActionBar = new QWidget(pageListPanel);
    auto* pageActionLayout = new QHBoxLayout(pageActionBar);
    pageActionLayout->setContentsMargins(0, 0, 0, 0);
    pageActionLayout->setSpacing(4);
    m_movePageUpButton = new QToolButton(pageActionBar);
    m_movePageDownButton = new QToolButton(pageActionBar);
    m_removePageButton = new QToolButton(pageActionBar);
    m_movePageUpButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_movePageDownButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_removePageButton->setIcon(
        QIcon::fromTheme(QStringLiteral("user-trash"), style()->standardIcon(QStyle::SP_TrashIcon)));
    m_movePageUpButton->setToolTip(tr("Move selected pages up"));
    m_movePageDownButton->setToolTip(tr("Move selected pages down"));
    m_removePageButton->setToolTip(tr("Remove selected pages from the project"));
    m_movePageUpButton->setAutoRaise(true);
    m_movePageDownButton->setAutoRaise(true);
    m_removePageButton->setAutoRaise(true);
    pageActionLayout->addWidget(m_movePageUpButton);
    pageActionLayout->addWidget(m_movePageDownButton);
    pageActionLayout->addWidget(m_removePageButton);
    pageActionLayout->addStretch();
    pageListLayout->addWidget(pageActionBar);

    m_pageOrderModel = new PageOrderListModel(m_project, this);
    m_pageTable = new QTableView(pageListPanel);
    m_pageTable->setModel(m_pageOrderModel);
    m_pageTable->setMinimumWidth(320);
    m_pageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pageTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pageTable->setDragEnabled(true);
    m_pageTable->setAcceptDrops(true);
    m_pageTable->setDropIndicatorShown(true);
    m_pageTable->setDragDropMode(QAbstractItemView::InternalMove);
    m_pageTable->setDefaultDropAction(Qt::MoveAction);
    m_pageTable->setDragDropOverwriteMode(false);
    m_pageTable->verticalHeader()->setVisible(false);
    m_pageTable->setColumnWidth(PageOrderListModel::ImageNameColumn, defaultPageOrderImageNameColumnWidth);
    m_pageTable->setColumnWidth(PageOrderListModel::OriginalIndexColumn, defaultPageOrderOriginalIndexColumnWidth);
    restoreTableColumnWidths();
    m_pageTableColumns = new ViewportFittedTableColumns(
        m_pageTable,
        {
            {PageOrderListModel::ImageNameColumn, defaultPageOrderImageNameColumnWidth,
             minimumPageOrderImageNameColumnWidth, true},
            {PageOrderListModel::OriginalIndexColumn, defaultPageOrderOriginalIndexColumnWidth,
             minimumPageOrderOriginalIndexColumnWidth, false},
        },
        this);
    m_pageTableColumns->fitToViewport();
    pageListLayout->addWidget(m_pageTable, 1);

    splitter->addWidget(pageListPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    rootLayout->addWidget(splitter, 1);

    connect(m_pageTable->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { updatePreview(current); });
    connect(m_pageTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { updatePageActionButtons(); });
    connect(m_pageTable->horizontalHeader(), &QHeaderView::sectionResized, m_pageTableColumns,
            &ViewportFittedTableColumns::rebalanceFromSectionResize);
    connect(m_movePageUpButton, &QToolButton::clicked, this, [this]() {
        if (m_pageOrderModel != nullptr) {
            m_pageOrderModel->moveSourceIndexes(selectedPageSourceIndexes(), -1);
        }
    });
    connect(m_movePageDownButton, &QToolButton::clicked, this, [this]() {
        if (m_pageOrderModel != nullptr) {
            m_pageOrderModel->moveSourceIndexes(selectedPageSourceIndexes(), 1);
        }
    });
    connect(m_removePageButton, &QToolButton::clicked, this, [this]() {
        if (m_pageOrderModel != nullptr) {
            m_pageOrderModel->removeSourceIndexes(selectedPageSourceIndexes());
        }
    });
    connect(m_pageOrderModel, &QAbstractItemModel::modelReset, this, [this]() {
        if (m_pageTable == nullptr || m_pageOrderModel == nullptr || m_pageOrderModel->rowCount() <= 0) {
            updatePageActionButtons();
            return;
        }

        const QVector<int> movedSourceIndexes = m_pageOrderModel->lastMovedSourceIndexes();
        if (!movedSourceIndexes.isEmpty()) {
            QItemSelection selection;
            for (int sourceIndex : movedSourceIndexes) {
                const int row = m_pageOrderModel->rowForSourceIndex(sourceIndex);
                if (row >= 0) {
                    const QModelIndex index = m_pageOrderModel->index(row, 0);
                    selection.select(index, index);
                }
            }
            if (!selection.isEmpty()) {
                m_pageTable->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
                const int currentRow = m_pageOrderModel->rowForSourceIndex(movedSourceIndexes.first());
                if (currentRow >= 0) {
                    m_pageTable->setCurrentIndex(m_pageOrderModel->index(currentRow, 0));
                }
                updatePageActionButtons();
                return;
            }
        }

        if (!m_pageTable->currentIndex().isValid()) {
            m_pageTable->setCurrentIndex(m_pageOrderModel->index(0, 0));
        }
        updatePageActionButtons();
    });
    if (m_pageOrderModel->rowCount() > 0) {
        m_pageTable->setCurrentIndex(m_pageOrderModel->index(0, 0));
    }
    updatePageActionButtons();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

void PageOrderDialog::updatePreview(const QModelIndex& currentIndex)
{
    if (m_canvas == nullptr || m_pageOrderModel == nullptr || !currentIndex.isValid()) {
        return;
    }

    const int sourceIndex = m_pageOrderModel->sourceIndexForRow(currentIndex.row());
    if (sourceIndex < 0 || sourceIndex >= m_project.images().size()) {
        return;
    }

    const labelqt::core::ImageEntry& image = m_project.images().at(sourceIndex);
    m_canvas->setImage(image.path, image.labels);
}

void PageOrderDialog::updatePageActionButtons()
{
    if (m_pageOrderModel == nullptr) {
        return;
    }

    const QVector<int> selectedSourceIndexes = selectedPageSourceIndexes();
    const bool hasSelection = !selectedSourceIndexes.isEmpty();
    bool canMoveUp = false;
    bool canMoveDown = false;
    for (int sourceIndex : selectedSourceIndexes) {
        const int row = m_pageOrderModel->rowForSourceIndex(sourceIndex);
        if (row > 0) {
            canMoveUp = true;
        }
        if (row >= 0 && row < m_pageOrderModel->rowCount() - 1) {
            canMoveDown = true;
        }
    }

    if (m_movePageUpButton != nullptr) {
        m_movePageUpButton->setEnabled(canMoveUp);
    }
    if (m_movePageDownButton != nullptr) {
        m_movePageDownButton->setEnabled(canMoveDown);
    }
    if (m_removePageButton != nullptr) {
        m_removePageButton->setEnabled(hasSelection);
    }
}

void PageOrderDialog::restoreTableColumnWidths()
{
    if (m_pageTable == nullptr || m_pageTable->horizontalHeader() == nullptr) {
        return;
    }

    QHeaderView* header = m_pageTable->horizontalHeader();
    const QSignalBlocker blocker(header);
    header->resizeSection(
        PageOrderListModel::OriginalIndexColumn,
        std::max(minimumPageOrderOriginalIndexColumnWidth,
                 m_sessionStateStore.pageOrderOriginalIndexColumnWidth(defaultPageOrderOriginalIndexColumnWidth)));
}

void PageOrderDialog::saveTableColumnWidths() const
{
    if (m_pageTable != nullptr && m_pageTable->horizontalHeader() != nullptr) {
        m_sessionStateStore.savePageOrderOriginalIndexColumnWidth(
            m_pageTable->columnWidth(PageOrderListModel::OriginalIndexColumn));
    }
}

QVector<int> PageOrderDialog::selectedPageSourceIndexes() const
{
    QVector<int> sourceIndexes;
    if (m_pageTable == nullptr || m_pageOrderModel == nullptr || m_pageTable->selectionModel() == nullptr) {
        return sourceIndexes;
    }

    const QModelIndexList selectedIndexes = m_pageTable->selectionModel()->selectedRows();
    sourceIndexes.reserve(selectedIndexes.size());
    for (const QModelIndex& index : selectedIndexes) {
        const int sourceIndex = m_pageOrderModel->sourceIndexForRow(index.row());
        if (sourceIndex >= 0 && !sourceIndexes.contains(sourceIndex)) {
            sourceIndexes.append(sourceIndex);
        }
    }
    return sourceIndexes;
}
