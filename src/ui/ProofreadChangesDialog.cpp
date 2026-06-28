#include "ui/ProofreadChangesDialog.h"

#include "services/ProjectComparisonService.h"
#include "services/ProofreadReportService.h"
#include "services/TextDiffHtmlRenderer.h"
#include "ui/DialogWindowUtils.h"
#include "ui/ImageCanvas.h"
#include "ui/ViewportFittedTableColumns.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

#include <expected>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int changeIndexRole = Qt::UserRole + 1;
constexpr int sortTextRole = Qt::UserRole + 2;
constexpr int sortNumberRole = Qt::UserRole + 3;
constexpr int textCellVerticalMargin = 10;

enum Column {
    PageColumn,
    NumberColumn,
    KindColumn,
    SummaryColumn,
    ColumnCount,
};

QString labelNumber(int labelIndex)
{
    return QString::number(labelIndex + 1).rightJustified(3, QLatin1Char('0'));
}

QVector<int> defaultColumnWidths()
{
    return {160, 80, 120, 220};
}

class ProofreadChangeTableItem final : public QTableWidgetItem {
public:
    ProofreadChangeTableItem(QString text, int changeIndex, QString sortText, int sortNumber)
        : QTableWidgetItem(std::move(text))
    {
        setData(changeIndexRole, changeIndex);
        setData(sortTextRole, std::move(sortText));
        setData(sortNumberRole, sortNumber);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        const QString lhsText = data(sortTextRole).toString();
        const QString rhsText = other.data(sortTextRole).toString();
        const int textComparison = QString::localeAwareCompare(lhsText, rhsText);
        if (textComparison != 0) {
            return textComparison < 0;
        }
        return data(sortNumberRole).toInt() < other.data(sortNumberRole).toInt();
    }
};

QTableWidgetItem* changeItem(QString text, int changeIndex, QString sortText, int sortNumber)
{
    auto* item = new ProofreadChangeTableItem(std::move(text), changeIndex, std::move(sortText), sortNumber);
    item->setToolTip(item->text());
    return item;
}

QPlainTextEdit* readOnlyCellTextEdit(const QString& text, QWidget* parent)
{
    auto* editor = new QPlainTextEdit(parent);
    editor->setPlainText(text);
    editor->setReadOnly(true);
    editor->setFrameShape(QFrame::NoFrame);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor->setWordWrapMode(QTextOption::WordWrap);
    editor->setFocusPolicy(Qt::WheelFocus);
    editor->viewport()->setCursor(Qt::ArrowCursor);
    editor->setToolTip(text);
    return editor;
}

const labelqt::core::ImageEntry* imageByName(const labelqt::core::Project& project, const QString& imageName)
{
    for (const labelqt::core::ImageEntry& image : project.images()) {
        if (image.name == imageName) {
            return &image;
        }
    }
    return nullptr;
}

QPointF focusPositionForChange(const labelqt::services::ReviewChange& change)
{
    if (change.kind != labelqt::services::ReviewChangeKind::Deleted) {
        return change.current.position;
    }
    return change.baseline.position;
}

QString baselineImageNameForChange(const labelqt::services::ReviewChange& change)
{
    return change.baselineImageName.isEmpty() ? change.imageName : change.baselineImageName;
}

int wrappedPlainTextHeight(const QString& text, const QFont& font, int width)
{
    QTextDocument document;
    document.setDefaultFont(font);
    document.setPlainText(text);

    QTextOption option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WordWrap);
    document.setDefaultTextOption(option);
    document.setTextWidth(std::max(1, width));

    const QFontMetrics metrics(font);
    return std::max(metrics.lineSpacing(), static_cast<int>(std::ceil(document.size().height())));
}
} // namespace

ProofreadChangesDialog::ProofreadChangesDialog(const labelqt::core::Project& beforeProject,
                                               const labelqt::core::Project& currentProject,
                                               labelqt::core::AppPreferences preferences,
                                               labelqt::services::ReviewMetadata metadata,
                                               QVector<labelqt::services::ReviewChange> changes, QWidget* parent)
    : QDialog(parent), m_beforeProject(beforeProject), m_currentProject(currentProject),
      m_preferences(std::move(preferences)), m_metadata(std::move(metadata)), m_changes(std::move(changes))
{
    buildUi();
}

ProofreadChangesDialog::~ProofreadChangesDialog()
{
    if (m_tableColumns != nullptr) {
        m_tableColumns->setColumnsChangedCallback({});
    }
    if (m_table != nullptr) {
        disconnect(m_table, nullptr, this, nullptr);
    }
    if (m_beforeCanvas != nullptr) {
        disconnect(m_beforeCanvas, nullptr, this, nullptr);
    }
    if (m_afterCanvas != nullptr) {
        disconnect(m_afterCanvas, nullptr, this, nullptr);
    }
}

int ProofreadChangesDialog::selectedImageIndex() const noexcept
{
    const int changeIndex = changeIndexForRow(m_table == nullptr ? -1 : m_table->currentRow());
    if (changeIndex < 0) {
        return -1;
    }
    return m_changes.at(changeIndex).imageIndex;
}

int ProofreadChangesDialog::selectedLabelIndex() const noexcept
{
    const int changeIndex = changeIndexForRow(m_table == nullptr ? -1 : m_table->currentRow());
    if (changeIndex < 0) {
        return -1;
    }
    return m_changes.at(changeIndex).currentLabelIndex;
}

void ProofreadChangesDialog::done(int result)
{
    m_isClosing = true;
    saveTableColumnWidths();
    QDialog::done(result);
}

void ProofreadChangesDialog::buildUi()
{
    setWindowTitle(tr("Proofreading Changes"));
    labelqt::ui::configureLargeDialogWindow(*this, QSize(1280, 820));

    auto* rootLayout = new QVBoxLayout(this);
    m_summaryLabel = new QLabel(tr("%n proofreading change(s)", nullptr, static_cast<int>(m_changes.size())), this);
    rootLayout->addWidget(m_summaryLabel);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Page"), QStringLiteral("#"), tr("Change"), tr("Summary")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    restoreTableColumnWidths();
    m_table->setMinimumWidth(520);
    m_tableColumns = new ViewportFittedTableColumns(
        m_table,
        {
            {PageColumn, 160, 80, false},
            {NumberColumn, 80, 52, false},
            {KindColumn, 120, 80, false},
            {SummaryColumn, 220, 120, true},
        },
        m_table);
    m_tableColumns->setColumnsChangedCallback([this]() { resizeTableRowsToContents(); });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int oldSize, int newSize) {
                if (m_tableColumns != nullptr) {
                    m_tableColumns->rebalanceFromSectionResize(logicalIndex, oldSize, newSize);
                }
                resizeTableRowsToContents();
            });
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this]() {
        resizeTableRowsToContents();
        scheduleDetailsUpdate(m_table == nullptr ? -1 : m_table->currentRow());
    });
    splitter->addWidget(m_table);

    auto* detailPanel = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);

    detailLayout->addWidget(new QLabel(tr("Text Difference"), detailPanel));
    m_diffBrowser = new QTextBrowser(detailPanel);
    m_diffBrowser->setOpenExternalLinks(false);
    detailLayout->addWidget(m_diffBrowser, 1);

    auto* previewSplitter = new QSplitter(Qt::Horizontal, detailPanel);
    auto* beforeGroup = new QGroupBox(tr("Before"), previewSplitter);
    auto* beforeLayout = new QVBoxLayout(beforeGroup);
    m_beforeCanvas = new ImageCanvas(beforeGroup);
    m_beforeCanvas->setReadOnly(true);
    m_beforeCanvas->setPreferences(m_preferences);
    m_beforeCanvas->setGroups(m_currentProject.groups());
    m_beforeCanvas->setVisibleGroups(m_currentProject.groups());
    beforeLayout->addWidget(m_beforeCanvas);

    auto* afterGroup = new QGroupBox(tr("After"), previewSplitter);
    auto* afterLayout = new QVBoxLayout(afterGroup);
    m_afterCanvas = new ImageCanvas(afterGroup);
    m_afterCanvas->setReadOnly(true);
    m_afterCanvas->setPreferences(m_preferences);
    m_afterCanvas->setGroups(m_currentProject.groups());
    m_afterCanvas->setVisibleGroups(m_currentProject.groups());
    afterLayout->addWidget(m_afterCanvas);

    previewSplitter->addWidget(beforeGroup);
    previewSplitter->addWidget(afterGroup);
    previewSplitter->setStretchFactor(0, 1);
    previewSplitter->setStretchFactor(1, 1);
    detailLayout->addWidget(previewSplitter, 2);

    splitter->addWidget(detailPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    connect(m_beforeCanvas, &ImageCanvas::viewportStateChanged, this,
            [this](int zoomPercent, QPointF normalizedCenter) {
                syncPreviewCanvases(m_beforeCanvas, zoomPercent, normalizedCenter);
            });
    connect(m_afterCanvas, &ImageCanvas::viewportStateChanged, this,
            [this](int zoomPercent, QPointF normalizedCenter) {
                syncPreviewCanvases(m_afterCanvas, zoomPercent, normalizedCenter);
            });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_jumpButton = buttons->addButton(tr("Jump to Label"), QDialogButtonBox::AcceptRole);
    auto* exportButton = buttons->addButton(tr("Export HTML..."), QDialogButtonBox::ActionRole);
    connect(m_jumpButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(exportButton, &QPushButton::clicked, this, &ProofreadChangesDialog::exportReport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    populateRows();
    m_table->setSortingEnabled(true);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int currentRow) {
        scheduleDetailsUpdate(currentRow);
        updateJumpButton();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (changeIndexForRow(row) >= 0) {
            m_isClosing = true;
            QTimer::singleShot(0, this, &QDialog::accept);
        }
    });

    if (!m_changes.isEmpty()) {
        m_table->setCurrentCell(0, 0);
    }
    resizeTableRowsToContents();
    updateDetails(m_table->currentRow());
    updateJumpButton();
}

void ProofreadChangesDialog::populateRows()
{
    if (m_table == nullptr) {
        return;
    }

    m_table->setSortingEnabled(false);
    m_table->setRowCount(static_cast<int>(m_changes.size()));
    for (int changeIndex = 0; changeIndex < m_changes.size(); ++changeIndex) {
        const labelqt::services::ReviewChange& change = m_changes.at(changeIndex);
        const QString number = labelNumber(change.labelIndex);
        const QString kind = changeKindText(change.kind);
        const QString summary = changeSummary(change);
        const int sortNumber = change.labelIndex < 0 ? std::numeric_limits<int>::max() : change.labelIndex;

        m_table->setItem(changeIndex, PageColumn, changeItem(change.imageName, changeIndex, change.imageName, sortNumber));
        m_table->setItem(changeIndex, NumberColumn, changeItem(number, changeIndex, QString(), sortNumber));
        m_table->setItem(changeIndex, KindColumn, changeItem(kind, changeIndex, kind, sortNumber));
        m_table->setItem(changeIndex, SummaryColumn, changeItem(summary, changeIndex, summary, sortNumber));
        m_table->setCellWidget(changeIndex, SummaryColumn, readOnlyCellTextEdit(summary, m_table));
    }
}

void ProofreadChangesDialog::restoreTableColumnWidths()
{
    if (m_table == nullptr) {
        return;
    }

    const QVector<int> widths = m_sessionStateStore.proofreadingChangesColumnWidths(defaultColumnWidths());
    for (int column = 0; column < widths.size() && column < m_table->columnCount(); ++column) {
        m_table->setColumnWidth(column, widths.at(column));
    }
}

void ProofreadChangesDialog::saveTableColumnWidths() const
{
    if (m_table == nullptr) {
        return;
    }

    QVector<int> widths;
    widths.reserve(m_table->columnCount());
    for (int column = 0; column < m_table->columnCount(); ++column) {
        widths.append(m_table->columnWidth(column));
    }
    m_sessionStateStore.saveProofreadingChangesColumnWidths(widths);
}

void ProofreadChangesDialog::exportReport()
{
    if (m_changes.isEmpty()) {
        QMessageBox::information(this, tr("Proofreading"), tr("No proofreading changes to export."));
        return;
    }

    QString defaultPath = m_sessionStateStore.lastFileDialogDirectory();
    const QString baseName = QFileInfo(m_currentProject.filePath()).completeBaseName();
    const QString fileName = baseName.isEmpty() ? QStringLiteral("proofreading-report.html")
                                                : QStringLiteral("%1-proofreading-report.html").arg(baseName);
    defaultPath = defaultPath.isEmpty() ? fileName : QDir(defaultPath).filePath(fileName);

    const QString path = QFileDialog::getSaveFileName(this, tr("Export proofreading report"), defaultPath,
                                                      tr("HTML files (*.html);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    m_sessionStateStore.saveLastFileDialogPath(path);

    const QString sourceDescription = m_currentProject.filePath().isEmpty()
        ? QString()
        : tr("Project: %1").arg(QDir::toNativeSeparators(m_currentProject.filePath()));
    const std::expected<void, QString> result =
        labelqt::services::ProofreadReportService::saveHtmlReport(path, m_changes, reportTexts(), sourceDescription);
    if (!result.has_value()) {
        QMessageBox::warning(this, tr("Proofreading"),
                             tr("Failed to export proofreading report: %1").arg(result.error()));
        return;
    }

    QMessageBox::information(this, tr("Proofreading"), tr("Proofreading report exported."));
}

int ProofreadChangesDialog::changeIndexForRow(int row) const
{
    if (m_table == nullptr || row < 0 || row >= m_table->rowCount()) {
        return -1;
    }

    const QTableWidgetItem* item = m_table->item(row, PageColumn);
    if (item == nullptr) {
        return -1;
    }

    const int changeIndex = item->data(changeIndexRole).toInt();
    return changeIndex >= 0 && changeIndex < m_changes.size() ? changeIndex : -1;
}

void ProofreadChangesDialog::scheduleDetailsUpdate(int row)
{
    if (m_isClosing) {
        return;
    }

    m_pendingDetailRow = row;
    if (m_detailUpdateQueued) {
        return;
    }

    m_detailUpdateQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_detailUpdateQueued = false;
        if (m_isClosing) {
            return;
        }
        updateDetails(m_pendingDetailRow);
    });
}

void ProofreadChangesDialog::updateDetails(int row)
{
    if (m_isClosing) {
        return;
    }
    const int changeIndex = changeIndexForRow(row);
    if (m_diffBrowser == nullptr || changeIndex < 0) {
        return;
    }

    const labelqt::services::ReviewChange& change = m_changes.at(changeIndex);
    const QString beforeText =
        change.kind == labelqt::services::ReviewChangeKind::Added ? QString() : change.baseline.text;
    const QString afterText =
        change.kind == labelqt::services::ReviewChangeKind::Deleted ? QString() : change.current.text;
    m_diffBrowser->setHtml(diffHtml(beforeText, afterText));
    updatePreviewCanvases(change);
}

void ProofreadChangesDialog::resizeTableRowsToContents()
{
    if (m_table == nullptr) {
        return;
    }

    for (int row = 0; row < m_table->rowCount(); ++row) {
        resizeTableRowToContent(row);
    }
}

void ProofreadChangesDialog::resizeTableRowToContent(int row)
{
    if (m_table == nullptr || row < 0 || row >= m_table->rowCount()) {
        return;
    }

    const QFontMetrics metrics(m_table->font());
    const int maximumTextRows = std::max(1, m_preferences.labelTableMaxTextRows());
    const int contentHeight = metrics.lineSpacing() * maximumTextRows;
    const int maximumHeight =
        std::max(m_table->verticalHeader()->minimumSectionSize(), contentHeight + textCellVerticalMargin);

    int naturalHeight = metrics.lineSpacing() + textCellVerticalMargin;
    const QTableWidgetItem* summaryItem = m_table->item(row, SummaryColumn);
    if (summaryItem != nullptr) {
        const int textWidth = std::max(
            1, m_table->columnWidth(SummaryColumn) - 2 * m_table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin));
        naturalHeight =
            std::max(naturalHeight, wrappedPlainTextHeight(summaryItem->text(), m_table->font(), textWidth) +
                                        textCellVerticalMargin);
    }

    const int rowHeight = std::clamp(naturalHeight, m_table->verticalHeader()->minimumSectionSize(), maximumHeight);
    m_table->setRowHeight(row, rowHeight);
}

void ProofreadChangesDialog::updatePreviewCanvases(const labelqt::services::ReviewChange& change)
{
    if (m_beforeCanvas == nullptr || m_afterCanvas == nullptr) {
        return;
    }

    const QString baselineImageName = baselineImageNameForChange(change);
    const labelqt::core::ImageEntry* beforeImage = imageByName(m_beforeProject, baselineImageName);
    const labelqt::core::ImageEntry* currentImage = imageByName(m_currentProject, change.imageName);
    const labelqt::core::ImageEntry* displayImage = currentImage != nullptr ? currentImage : beforeImage;
    if (displayImage == nullptr) {
        m_beforeCanvas->setImage(QString(), {});
        m_afterCanvas->setImage(QString(), {});
        return;
    }

    const int zoomPercent = m_afterCanvas->zoomPercent();
    const QPointF normalizedCenter = focusPositionForChange(change);
    const QVector<labelqt::core::Label> baselineLabels =
        labelqt::services::ProjectComparisonService::baselineImageLabels(m_currentProject, m_metadata, change.imageName,
                                                                         baselineImageName);

    m_isSyncingCanvasViews = true;
    m_beforeCanvas->setImage(beforeImage == nullptr ? displayImage->path : beforeImage->path, baselineLabels);
    m_afterCanvas->setImage(displayImage->path, currentImage == nullptr ? QVector<labelqt::core::Label>{}
                                                                        : currentImage->labels);
    m_beforeCanvas->setSelectedLabel(change.baselineLabelIndex);
    m_afterCanvas->setSelectedLabel(change.currentLabelIndex);
    m_beforeCanvas->restoreView(zoomPercent, normalizedCenter);
    m_afterCanvas->restoreView(zoomPercent, normalizedCenter);
    m_isSyncingCanvasViews = false;
}

void ProofreadChangesDialog::updateJumpButton()
{
    if (m_jumpButton == nullptr) {
        return;
    }

    const int imageIndex = selectedImageIndex();
    const int labelIndex = selectedLabelIndex();
    const bool canJumpToLabel =
        imageIndex >= 0 && imageIndex < m_currentProject.images().size() && labelIndex >= 0 &&
        labelIndex < m_currentProject.images().at(imageIndex).labels.size() &&
        !m_currentProject.images().at(imageIndex).labels.at(labelIndex).isDeleted();
    const bool canJumpToPage = imageIndex >= 0 && imageIndex < m_currentProject.images().size();
    m_jumpButton->setEnabled(canJumpToLabel || canJumpToPage);
}

QString ProofreadChangesDialog::changeKindText(labelqt::services::ReviewChangeKind kind) const
{
    switch (kind) {
    case labelqt::services::ReviewChangeKind::Added:
        return tr("Added");
    case labelqt::services::ReviewChangeKind::Deleted:
        return tr("Deleted");
    case labelqt::services::ReviewChangeKind::Modified:
        return tr("Modified");
    }
    return tr("Modified");
}

QString ProofreadChangesDialog::changeSummary(const labelqt::services::ReviewChange& change) const
{
    if (change.kind != labelqt::services::ReviewChangeKind::Modified) {
        return changeKindText(change.kind);
    }

    QStringList parts;
    if (change.textChanged) {
        parts.append(tr("text"));
    }
    if (change.groupChanged) {
        parts.append(tr("group"));
    }
    if (change.positionChanged) {
        parts.append(tr("marker"));
    }
    if (change.orderChanged) {
        parts.append(tr("order"));
    }
    return parts.isEmpty() ? tr("Modified") : parts.join(QStringLiteral(", "));
}

QString ProofreadChangesDialog::diffHtml(const QString& beforeText, const QString& afterText) const
{
    return labelqt::services::TextDiffHtmlRenderer::renderInlineDiff(beforeText, afterText, tr("No text change."));
}

labelqt::services::ProofreadReportTexts ProofreadChangesDialog::reportTexts() const
{
    return {
        .title = tr("Proofreading Report"),
        .generatedAt = tr("Generated at"),
        .totalChanges = tr("Total changes"),
        .page = tr("Page"),
        .label = tr("Label"),
        .changeType = tr("Change Type"),
        .summary = tr("Summary"),
        .textDifference = tr("Text Difference"),
        .groupChange = tr("Group Change"),
        .markerChange = tr("Marker Change"),
        .orderChange = tr("Order Change"),
        .before = tr("Before"),
        .after = tr("After"),
        .added = tr("Added"),
        .deleted = tr("Deleted"),
        .modified = tr("Modified"),
        .text = tr("text"),
        .group = tr("group"),
        .marker = tr("marker"),
        .order = tr("order"),
        .noTextChange = tr("No text change."),
    };
}

void ProofreadChangesDialog::syncPreviewCanvases(ImageCanvas* sourceCanvas, int zoomPercent, QPointF normalizedCenter)
{
    if (m_isSyncingCanvasViews || sourceCanvas == nullptr) {
        return;
    }

    m_isSyncingCanvasViews = true;
    if (m_beforeCanvas != nullptr && m_beforeCanvas != sourceCanvas) {
        m_beforeCanvas->restoreView(zoomPercent, normalizedCenter);
    }
    if (m_afterCanvas != nullptr && m_afterCanvas != sourceCanvas) {
        m_afterCanvas->restoreView(zoomPercent, normalizedCenter);
    }
    m_isSyncingCanvasViews = false;
}
