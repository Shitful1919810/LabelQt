#include "ui/ProofreadChangesDialog.h"

#include "services/ProjectComparisonService.h"
#include "services/TextDiffService.h"
#include "ui/DialogWindowUtils.h"
#include "ui/ImageCanvas.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <utility>

namespace {
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

QString htmlText(QString text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString wrappedSpan(const QString& text, const QString& style)
{
    return QStringLiteral("<span style=\"%1\">%2</span>").arg(style, htmlText(text));
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

int ProofreadChangesDialog::selectedImageIndex() const noexcept
{
    if (m_table == nullptr || m_table->currentRow() < 0 || m_table->currentRow() >= m_changes.size()) {
        return -1;
    }
    return m_changes.at(m_table->currentRow()).imageIndex;
}

int ProofreadChangesDialog::selectedLabelIndex() const noexcept
{
    if (m_table == nullptr || m_table->currentRow() < 0 || m_table->currentRow() >= m_changes.size()) {
        return -1;
    }
    return m_changes.at(m_table->currentRow()).currentLabelIndex;
}

void ProofreadChangesDialog::done(int result)
{
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
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    restoreTableColumnWidths();
    m_table->setMinimumWidth(520);
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
    connect(m_jumpButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    populateRows();
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int currentRow) {
                updateDetails(currentRow);
                updateJumpButton();
            });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_changes.size()) {
            accept();
        }
    });

    if (!m_changes.isEmpty()) {
        m_table->setCurrentCell(0, 0);
    }
    updateDetails(m_table->currentRow());
    updateJumpButton();
}

void ProofreadChangesDialog::populateRows()
{
    if (m_table == nullptr) {
        return;
    }

    m_table->setRowCount(static_cast<int>(m_changes.size()));
    for (int row = 0; row < m_changes.size(); ++row) {
        const labelqt::services::ReviewChange& change = m_changes.at(row);
        m_table->setItem(row, PageColumn, new QTableWidgetItem(change.imageName));
        m_table->setItem(row, NumberColumn, new QTableWidgetItem(labelNumber(change.labelIndex)));
        m_table->setItem(row, KindColumn, new QTableWidgetItem(changeKindText(change.kind)));
        m_table->setItem(row, SummaryColumn, new QTableWidgetItem(changeSummary(change)));
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

void ProofreadChangesDialog::updateDetails(int row)
{
    if (m_diffBrowser == nullptr || row < 0 || row >= m_changes.size()) {
        return;
    }

    const labelqt::services::ReviewChange& change = m_changes.at(row);
    const QString beforeText =
        change.kind == labelqt::services::ReviewChangeKind::Added ? QString() : change.baseline.text;
    const QString afterText =
        change.kind == labelqt::services::ReviewChangeKind::Deleted ? QString() : change.current.text;
    m_diffBrowser->setHtml(diffHtml(beforeText, afterText));
    updatePreviewCanvases(change);
}

void ProofreadChangesDialog::updatePreviewCanvases(const labelqt::services::ReviewChange& change)
{
    if (m_beforeCanvas == nullptr || m_afterCanvas == nullptr) {
        return;
    }

    const labelqt::core::ImageEntry* beforeImage = imageByName(m_beforeProject, change.imageName);
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
        labelqt::services::ProjectComparisonService::baselineImageLabels(m_currentProject, m_metadata, change.imageName);

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
    if (beforeText == afterText) {
        return QStringLiteral("<p>%1</p>").arg(tr("No text change."));
    }

    const QVector<labelqt::services::TextDiffChunk> chunks = labelqt::services::TextDiffService::diff(beforeText, afterText);
    QString html = QStringLiteral("<p style=\"line-height:1.45\">");
    for (const labelqt::services::TextDiffChunk& chunk : chunks) {
        if (chunk.text.isEmpty()) {
            continue;
        }
        switch (chunk.operation) {
        case labelqt::services::TextDiffOperation::Equal:
            html += htmlText(chunk.text);
            break;
        case labelqt::services::TextDiffOperation::Delete:
            html += wrappedSpan(chunk.text,
                                QStringLiteral("background:#7f1d1d;color:#ffffff;text-decoration:line-through;"));
            break;
        case labelqt::services::TextDiffOperation::Insert:
            html += wrappedSpan(chunk.text, QStringLiteral("background:#14532d;color:#ffffff;"));
            break;
        }
    }
    html += QStringLiteral("</p>");
    return html;
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
