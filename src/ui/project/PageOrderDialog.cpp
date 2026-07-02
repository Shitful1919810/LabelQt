#include "ui/project/PageOrderDialog.h"

#include "ui/widgets/DialogWindowUtils.h"
#include "ui/canvas/ImageCanvas.h"
#include "ui/project/ImageRenamePreviewWidget.h"
#include "ui/project/PageOrderListModel.h"
#include "ui/labeltable/ViewportFittedTableColumns.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QLabel>
#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int defaultPageOrderImageNameColumnWidth = 340;
constexpr int defaultPageOrderRenamePreviewColumnWidth = 300;
constexpr int defaultPageOrderRenameStatusColumnWidth = 300;
constexpr int defaultPageOrderOriginalIndexColumnWidth = 80;
constexpr int minimumPageOrderImageNameColumnWidth = 160;
constexpr int minimumPageOrderRenamePreviewColumnWidth = 160;
constexpr int minimumPageOrderRenameStatusColumnWidth = 100;
constexpr int minimumPageOrderOriginalIndexColumnWidth = 60;

class RenameStatusDelegate final : public QStyledItemDelegate {
public:
    explicit RenameStatusDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem adjustedOption(option);
        initStyleOption(&adjustedOption, index);
        const QVariant foreground = index.data(Qt::ForegroundRole);
        if (foreground.canConvert<QColor>()) {
            const QColor color = foreground.value<QColor>();
            adjustedOption.palette.setColor(QPalette::Text, color);
            adjustedOption.palette.setColor(QPalette::HighlightedText, color.lighter(145));
        }
        QStyledItemDelegate::paint(painter, adjustedOption, index);
    }
};
} // namespace

PageOrderDialog::PageOrderDialog(const labelqt::core::Project& project, labelqt::core::AppPreferences preferences,
                                 QWidget* parent, bool enableImageRename)
    : QDialog(parent), m_project(project), m_preferences(std::move(preferences)), m_enableImageRename(enableImageRename)
{
    buildUi();
}

QVector<int> PageOrderDialog::pageOrder() const
{
    return m_pageOrderModel == nullptr ? QVector<int>{} : m_pageOrderModel->pageOrder();
}

bool PageOrderDialog::hasPendingImageRenamePlan() const noexcept
{
    return m_hasPendingImageRenamePlan && m_pendingImageRenamePlan.hasChanges();
}

labelqt::services::ProjectImageRenamePlan PageOrderDialog::pendingImageRenamePlan() const
{
    return m_pendingImageRenamePlan;
}

void PageOrderDialog::setPendingImageRenameHandler(
    std::function<bool(const labelqt::services::ProjectImageRenamePlan&)> handler)
{
    m_pendingImageRenameHandler = std::move(handler);
}

void PageOrderDialog::done(int result)
{
    saveTableColumnWidths();
    QDialog::done(result);
}

void PageOrderDialog::buildUi()
{
    setWindowTitle(tr("Manage Pages"));
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
    m_pageTable->setItemDelegateForColumn(PageOrderListModel::RenameStatusColumn,
                                          new RenameStatusDelegate(m_pageTable));
    restoreTableColumnWidths();
    m_pageTable->setColumnHidden(PageOrderListModel::RenamePreviewColumn, !m_enableImageRename);
    m_pageTable->setColumnHidden(PageOrderListModel::RenameStatusColumn, !m_enableImageRename);
    QVector<ViewportFittedTableColumns::Column> columnSpecs = {
        {PageOrderListModel::ImageNameColumn, defaultPageOrderImageNameColumnWidth,
         minimumPageOrderImageNameColumnWidth, true},
        {PageOrderListModel::OriginalIndexColumn, defaultPageOrderOriginalIndexColumnWidth,
         minimumPageOrderOriginalIndexColumnWidth, false},
    };
    if (m_enableImageRename) {
        columnSpecs.insert(1, {PageOrderListModel::RenameStatusColumn, defaultPageOrderRenameStatusColumnWidth,
                               minimumPageOrderRenameStatusColumnWidth, true});
        columnSpecs.insert(1, {PageOrderListModel::RenamePreviewColumn, defaultPageOrderRenamePreviewColumnWidth,
                               minimumPageOrderRenamePreviewColumnWidth, true});
    }
    m_pageTableColumns = new ViewportFittedTableColumns(m_pageTable, std::move(columnSpecs), this);
    m_pageTableColumns->fitToViewport();
    pageListLayout->addWidget(m_pageTable, 3);

    if (m_enableImageRename) {
        auto* renameGroupBox = new QGroupBox(tr("Rename Images"), pageListPanel);
        auto* renameLayout = new QVBoxLayout(renameGroupBox);
        m_imageRenameWidget = new ImageRenamePreviewWidget(renameGroupBox);
        m_imageRenameWidget->setProject(m_project);
        connect(m_imageRenameWidget, &ImageRenamePreviewWidget::renameQueued, this,
                [this](const labelqt::services::ProjectImageRenamePlan& plan) {
                    m_pendingImageRenamePlan =
                        labelqt::services::ProjectImageRenameService::mergePlans(m_pendingImageRenamePlan, plan);
                    m_visibleImageRenamePlan = m_pendingImageRenamePlan;
                    m_hasPendingImageRenamePlan = m_pendingImageRenamePlan.hasChanges();
                    if (m_pageOrderModel != nullptr) {
                        m_pageOrderModel->setPendingRenamePlan(m_pendingImageRenamePlan);
                        m_pageOrderModel->setRenamePlan(m_pendingImageRenamePlan);
                    }
                    if (m_pageTable != nullptr) {
                        updatePreview(m_pageTable->currentIndex());
                    }
                });
        connect(m_imageRenameWidget, &ImageRenamePreviewWidget::previewChanged, this,
                [this](const labelqt::services::ProjectImageRenamePlan& plan) {
                    m_visibleImageRenamePlan =
                        labelqt::services::ProjectImageRenameService::mergePlans(m_pendingImageRenamePlan, plan);
                    if (m_pageOrderModel != nullptr) {
                        m_pageOrderModel->setRenamePlan(m_visibleImageRenamePlan);
                    }
                    if (m_pageTable != nullptr) {
                        updatePreview(m_pageTable->currentIndex());
                    }
                });
        renameLayout->addWidget(m_imageRenameWidget);
        renameGroupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        pageListLayout->addWidget(renameGroupBox);
    }

    splitter->addWidget(pageListPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    rootLayout->addWidget(splitter, 1);

    connect(m_pageTable->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { updatePreview(current); });
    connect(m_pageTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() {
                updatePageActionButtons();
                updateImageRenamePreview();
            });
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
                updateImageRenamePreview();
                return;
            }
        }

        if (!m_pageTable->currentIndex().isValid()) {
            m_pageTable->setCurrentIndex(m_pageOrderModel->index(0, 0));
        }
        updatePageActionButtons();
        updateImageRenamePreview();
    });
    if (m_pageOrderModel->rowCount() > 0) {
        m_pageTable->setCurrentIndex(m_pageOrderModel->index(0, 0));
    }
    updatePageActionButtons();
    updateImageRenamePreview();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PageOrderDialog::acceptRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

void PageOrderDialog::acceptRequested()
{
    if (hasPendingImageRenamePlan() && m_pendingImageRenameHandler) {
        if (!m_pendingImageRenameHandler(m_pendingImageRenamePlan)) {
            return;
        }
        m_pendingImageRenamePlan = {};
        m_visibleImageRenamePlan = {};
        m_hasPendingImageRenamePlan = false;
    }
    accept();
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

    const labelqt::core::ImageEntry image = previewImageForSourceIndex(sourceIndex);
    m_canvas->setImage(image.path, image.labels);
}

labelqt::core::ImageEntry PageOrderDialog::previewImageForSourceIndex(int sourceIndex) const
{
    labelqt::core::ImageEntry image = m_project.images().at(sourceIndex);
    const auto pendingIt =
        std::ranges::find_if(m_visibleImageRenamePlan.items, [sourceIndex](const auto& item) {
            return item.imageIndex == sourceIndex && item.included && item.referenceOnly && item.isChanged() &&
                   item.error.isEmpty();
        });
    if (pendingIt != m_visibleImageRenamePlan.items.cend()) {
        image.name = pendingIt->newName;
        image.path = pendingIt->newPath;
    }
    return image;
}

void PageOrderDialog::updateImageRenamePreview()
{
    if (m_imageRenameWidget == nullptr) {
        return;
    }
    m_imageRenameWidget->setTargetImageIndexes(renameTargetSourceIndexes());
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
    const QVector<int> defaultWidths = {
        defaultPageOrderImageNameColumnWidth,
        defaultPageOrderRenamePreviewColumnWidth,
        defaultPageOrderRenameStatusColumnWidth,
        defaultPageOrderOriginalIndexColumnWidth,
    };
    const QVector<int> minimumWidths = {
        minimumPageOrderImageNameColumnWidth,
        minimumPageOrderRenamePreviewColumnWidth,
        minimumPageOrderRenameStatusColumnWidth,
        minimumPageOrderOriginalIndexColumnWidth,
    };
    const QVector<int> widths = m_sessionStateStore.pageOrderColumnWidths(defaultWidths);
    const int columnCount = std::min(static_cast<int>(widths.size()), static_cast<int>(PageOrderListModel::ColumnCount));
    for (int column = 0; column < columnCount; ++column) {
        header->resizeSection(column, std::max(minimumWidths.at(column), widths.at(column)));
    }
}

void PageOrderDialog::saveTableColumnWidths() const
{
    if (m_pageTable != nullptr && m_pageTable->horizontalHeader() != nullptr) {
        QVector<int> widths;
        widths.reserve(PageOrderListModel::ColumnCount);
        for (int column = 0; column < PageOrderListModel::ColumnCount; ++column) {
            widths.append(m_pageTable->columnWidth(column));
        }
        m_sessionStateStore.savePageOrderColumnWidths(widths);
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

QVector<int> PageOrderDialog::renameTargetSourceIndexes() const
{
    if (m_pageOrderModel == nullptr) {
        return {};
    }

    const QVector<int> selectedSourceIndexes = selectedPageSourceIndexes();
    if (selectedSourceIndexes.isEmpty()) {
        return m_pageOrderModel->pageOrder();
    }

    QSet<int> selectedSet;
    selectedSet.reserve(selectedSourceIndexes.size());
    for (int sourceIndex : selectedSourceIndexes) {
        selectedSet.insert(sourceIndex);
    }

    QVector<int> orderedSelection;
    for (int sourceIndex : m_pageOrderModel->pageOrder()) {
        if (selectedSet.contains(sourceIndex)) {
            orderedSelection.append(sourceIndex);
        }
    }
    return orderedSelection;
}
