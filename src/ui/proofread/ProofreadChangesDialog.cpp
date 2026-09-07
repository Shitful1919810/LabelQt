#include "ui/proofread/ProofreadChangesDialog.h"

#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ProofreadReportService.h"
#include "services/proofread/TextDiffHtmlRenderer.h"
#include "ui/canvas/ImageCanvas.h"
#include "ui/labeltable/ViewportFittedTableColumns.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include "ui/proofread/ProofreadReviewPanel.h"
#include "ui/proofread/ReviewStatusDelegate.h"
#include "ui/widgets/BackgroundTask.h"
#include "ui/widgets/BusyTaskDialog.h"
#include "ui/widgets/CheckableFilterButton.h"
#include "ui/widgets/DialogWindowUtils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStyle>
#include <QTableView>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <cmath>
#include <expected>
#include <optional>
#include <utility>

namespace {
constexpr int textCellVerticalMargin = 10;
constexpr QLatin1StringView addedKindKey{"added"};
constexpr QLatin1StringView deletedKindKey{"deleted"};
constexpr QLatin1StringView modifiedKindKey{"modified"};

QVector<int> defaultColumnWidths()
{
    return {140, 60, 90, 180, 130};
}

QString temporaryExportPathFor(const QString& filePath)
{
    const QFileInfo fileInfo(filePath);
    const QString temporaryName =
        QStringLiteral(".%1.%2.tmp").arg(fileInfo.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces));
    return fileInfo.dir().filePath(temporaryName);
}

std::expected<void, QString> commitTemporaryExportFile(const QString& temporaryPath, const QString& targetPath)
{
    auto fail = [&temporaryPath](const QString& error) -> std::expected<void, QString> {
        QFile::remove(temporaryPath);
        return std::unexpected(error);
    };

    QFile temporaryFile(temporaryPath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        return fail(temporaryFile.errorString());
    }

    QSaveFile targetFile(targetPath);
    if (!targetFile.open(QIODevice::WriteOnly)) {
        return fail(targetFile.errorString());
    }

    QByteArray buffer;
    buffer.resize(64 * 1024);
    while (!temporaryFile.atEnd()) {
        const qint64 bytesRead = temporaryFile.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return fail(temporaryFile.errorString());
        }
        if (targetFile.write(buffer.constData(), bytesRead) != bytesRead) {
            return fail(targetFile.errorString());
        }
    }
    if (!targetFile.commit()) {
        return fail(targetFile.errorString());
    }
    QFile::remove(temporaryPath);
    return {};
}

std::optional<std::expected<void, QString>>
runProofreadReportExportTask(QWidget* parent, const QString& title, const QString& labelText, const QString& cancelText,
                             QString filePath, QVector<labelqt::services::ReviewChange> changes,
                             labelqt::core::Project beforeProject, labelqt::core::Project currentProject,
                             labelqt::services::ProofreadReportTexts texts, QString filterDescription,
                             labelqt::services::ProofreadReportOptions options)
{
    labelqt::ui::BusyTaskDialog progress(labelText, cancelText, parent);
    progress.setWindowTitle(title);

    const QString temporaryPath = temporaryExportPathFor(filePath);
    auto future =
        QtConcurrent::run([temporaryPath, changes = std::move(changes), beforeProject = std::move(beforeProject),
                           currentProject = std::move(currentProject), texts = std::move(texts),
                           filterDescription = std::move(filterDescription), options]() {
            try {
                return labelqt::services::ProofreadReportService::saveHtmlReport(
                    temporaryPath, changes, beforeProject, currentProject, texts, filterDescription, options);
            }
            catch (const std::exception& error) {
                return std::expected<void, QString>(std::unexpected(QString::fromUtf8(error.what())));
            }
            catch (...) {
                return std::expected<void, QString>(
                    std::unexpected(QCoreApplication::translate("MainWindow", "Unknown error")));
            }
        });
    const auto result = labelqt::ui::waitForBackgroundTask(std::move(future), progress,
                                                           [temporaryPath] { QFile::remove(temporaryPath); });
    if (!result)
        return std::nullopt;
    if (!result->has_value()) {
        QFile::remove(temporaryPath);
        return result;
    }
    return commitTemporaryExportFile(temporaryPath, filePath);
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

QString pageNameForReviewChange(const labelqt::services::ReviewChange& change)
{
    return change.imageName.isEmpty() ? change.baselineImageName : change.imageName;
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
                                               QVector<labelqt::services::ReviewChange> changes,
                                               ProofreadChangesDialogLabels labels, QWidget* parent)
    : QDialog(parent), m_beforeProject(beforeProject), m_currentProject(currentProject),
      m_preferences(std::move(preferences)), m_metadata(std::move(metadata)), m_labels(std::move(labels))
{
    if (m_labels.leftProjectTitle.isEmpty()) {
        m_labels.leftProjectTitle = tr("Before");
    }
    if (m_labels.rightProjectTitle.isEmpty()) {
        m_labels.rightProjectTitle = tr("After");
    }
    m_tableModel = new ProofreadChangeTableModel(
        std::move(changes),
        {
            .headers = {tr("Page"), QStringLiteral("#"), tr("Change"), tr("Summary"), tr("Review")},
            .added = tr("Added"),
            .deleted = tr("Deleted"),
            .modified = tr("Modified"),
            .facets =
                {
                    {labelqt::services::ReviewChangeFacet::Text, tr("text")},
                    {labelqt::services::ReviewChangeFacet::Format, tr("format")},
                    {labelqt::services::ReviewChangeFacet::Group, tr("group")},
                    {labelqt::services::ReviewChangeFacet::Marker, tr("marker")},
                    {labelqt::services::ReviewChangeFacet::Order, tr("order")},
                    {labelqt::services::ReviewChangeFacet::Comment, tr("Comment")},
                },
            .comment = tr("Comment only"),
        },
        this);
    QSet<QPair<QString, QString>> pageKeys;
    for (const auto& change : m_tableModel->changes()) {
        const auto key = qMakePair(change.baselineImageName, change.imageName);
        if (!pageKeys.contains(key)) {
            pageKeys.insert(key);
            m_pagePairs.append(
                {change.baselineImageName, change.imageName, labelqt::services::ProjectPageMatchKind::Name});
        }
    }
    buildUi();
    if (m_labels.reviewEnabled) {
        m_sessionKey = labelqt::services::ReviewSessionService::contextKey(m_beforeProject, m_currentProject);
        const auto session = m_labels.projectReviewSession
                                 ? labelqt::services::ReviewSessionService::decode(
                                       labelqt::services::ReviewSessionService::projectSession(m_currentProject))
                                 : labelqt::services::ReviewSessionService::loadLocal(m_sessionKey);
        if (session) {
            m_initialReviewSession = labelqt::services::ReviewSessionService::encode(*session);
            auto restored = *session;
            const auto valid = labelqt::services::ProjectComparisonService::changesWithPairs(
                m_currentProject, m_metadata, m_pagePairs, restored.pairs);
            if (!valid && !m_labels.roundReview) {
                restored.pairs.clear();
                QMessageBox::warning(this, tr("Manual pair"), valid.error());
            }
            m_reviewPanel->setSession(std::move(restored));
            if (!m_labels.roundReview) updateManualPairs();
            updateReviewAnnotations();
        }
        else {
            m_sessionWritable = false;
            m_reviewPanel->setEnabled(false);
            QMessageBox::warning(this, tr("Review"), session.error());
        }
    }
}

ProofreadChangesDialog::~ProofreadChangesDialog()
{
    for (QObject* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
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

const labelqt::services::ReviewChange* ProofreadChangesDialog::selectedChange() const
{
    return m_tableModel->changeAt(changeIndexForRow(m_table->currentIndex().row()));
}

int ProofreadChangesDialog::selectedImageIndex() const noexcept
{
    const int changeIndex = changeIndexForRow(m_table == nullptr ? -1 : m_table->currentIndex().row());
    if (changeIndex < 0) {
        return -1;
    }
    const labelqt::services::ReviewChange& change = m_tableModel->changes().at(changeIndex);
    if (m_labels.jumpToLeftProject) {
        return m_beforeProject.imageIndexByName(baselineImageNameForChange(change));
    }
    return change.imageIndex;
}

int ProofreadChangesDialog::selectedLabelIndex() const noexcept
{
    const int changeIndex = changeIndexForRow(m_table == nullptr ? -1 : m_table->currentIndex().row());
    if (changeIndex < 0) {
        return -1;
    }
    const labelqt::services::ReviewChange& change = m_tableModel->changes().at(changeIndex);
    return m_labels.jumpToLeftProject ? change.baselineLabelIndex : change.currentLabelIndex;
}

void ProofreadChangesDialog::done(int result)
{
    if (m_reviewPanel && m_sessionWritable) {
        if (!m_labels.projectReviewSession && reviewSession() != m_initialReviewSession) {
            const auto saved =
                labelqt::services::ReviewSessionService::saveLocal(m_sessionKey, m_reviewPanel->session());
            if (!saved) {
                if (QMessageBox::warning(this, tr("Review session not saved"), saved.error(),
                                         QMessageBox::Discard | QMessageBox::Cancel,
                                         QMessageBox::Cancel) != QMessageBox::Discard) {
                    m_isClosing = false;
                    return;
                }
            }
        }
    }
    m_isClosing = true;
    saveTableColumnWidths();
    QDialog::done(result);
}

void ProofreadChangesDialog::buildUi()
{
    setWindowTitle(tr("Proofreading Changes"));
    labelqt::ui::configureLargeDialogWindow(*this, QSize(1280, 820));

    auto* rootLayout = new QVBoxLayout(this);
    m_summaryLabel = new QLabel(tr("%n proofreading change(s)", nullptr, m_tableModel->rowCount()), this);
    rootLayout->addWidget(m_summaryLabel);

    auto* filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel(tr("Page"), this));
    m_pageFilterButton = new CheckableFilterButton(this);
    filterLayout->addWidget(m_pageFilterButton);
    filterLayout->addWidget(new QLabel(tr("Change"), this));
    m_kindFilterButton = new CheckableFilterButton(this);
    filterLayout->addWidget(m_kindFilterButton);
    filterLayout->addWidget(new QLabel(tr("Summary"), this));
    m_summaryFilterButton = new CheckableFilterButton(this);
    filterLayout->addWidget(m_summaryFilterButton);
    m_reviewFilter = new QComboBox(this);
    m_reviewFilter->addItems({tr("All review states"), tr("Unseen"), tr("Reviewed"), tr("Needs confirmation")});
    m_reviewFilter->setVisible(m_labels.reviewEnabled);
    filterLayout->addWidget(m_reviewFilter);
    auto* resetFilters = new QToolButton(this);
    resetFilters->setIcon(
        QIcon::fromTheme(QStringLiteral("edit-clear"), style()->standardIcon(QStyle::SP_DialogResetButton)));
    resetFilters->setToolTip(tr("Reset filters"));
    resetFilters->setAccessibleName(resetFilters->toolTip());
    connect(resetFilters, &QToolButton::clicked, this, [this] {
        const QSignalBlocker pageBlocker(m_pageFilterButton), kindBlocker(m_kindFilterButton),
            summaryBlocker(m_summaryFilterButton), reviewBlocker(m_reviewFilter);
        m_pageFilterButton->selectAll();
        m_kindFilterButton->selectAll();
        m_summaryFilterButton->selectAll();
        m_reviewFilter->setCurrentIndex(0);
        m_filterModel->setReviewStatusFilter(-1);
        applyFilters();
    });
    filterLayout->addWidget(resetFilters);
    filterLayout->addStretch(1);
    rootLayout->addLayout(filterLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_table = new QTableView(splitter);
    m_filterModel = new ProofreadChangeFilterModel(m_table);
    m_filterModel->setSourceModel(m_tableModel);
    m_table->setModel(m_filterModel);
    m_table->setItemDelegateForColumn(ProofreadChangeTableModel::ReviewColumn, new ReviewStatusDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideNone);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->setColumnHidden(ProofreadChangeTableModel::ReviewColumn, !m_labels.reviewEnabled);
    restoreTableColumnWidths();
    m_table->setMinimumWidth(520);
    m_tableColumns = new ViewportFittedTableColumns(m_table,
                                                    {
                                                        {ProofreadChangeTableModel::PageColumn, 160, 80, false},
                                                        {ProofreadChangeTableModel::NumberColumn, 80, 52, false},
                                                        {ProofreadChangeTableModel::KindColumn, 120, 80, false},
                                                        {ProofreadChangeTableModel::SummaryColumn, 220, 120, true},
                                                        {ProofreadChangeTableModel::ReviewColumn, 130, 100, false},
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
        scheduleDetailsUpdate(m_table == nullptr ? -1 : m_table->currentIndex().row());
    });
    splitter->addWidget(m_table);

    auto* detailPanel = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);
    if (m_labels.reviewEnabled) {
        m_reviewPanel = new ProofreadReviewPanel(
            {[this] { return selectedChanges(); }, [this] { return m_tableModel->changes(); },
             [this](const QVector<labelqt::services::ManualLabelPair>& pairs) -> std::expected<void, QString> {
                 const auto result = labelqt::services::ProjectComparisonService::changesWithPairs(
                     m_currentProject, m_metadata, m_pagePairs, pairs);
                 if (!result)
                     return std::unexpected(result.error());
                 return {};
             }},
            this, !m_labels.roundReview);
        connect(m_reviewPanel, &ProofreadReviewPanel::annotationsChanged, this,
                &ProofreadChangesDialog::updateReviewAnnotations);
        connect(m_reviewPanel, &ProofreadReviewPanel::pairsChanged, this, &ProofreadChangesDialog::updateManualPairs);
        connect(m_reviewPanel, &ProofreadReviewPanel::nextUnseenRequested, this, [this] {
            const int start = m_table->currentIndex().row();
            const int count = m_filterModel->rowCount();
            for (int offset = 1; offset <= count; ++offset) {
                const int row = (std::max(-1, start) + offset) % count;
                if (m_filterModel->index(row, 0).data(ProofreadChangeTableModel::ReviewStatusRole).toInt() == 0) {
                    m_table->selectRow(row);
                    return;
                }
            }
        });
    }
    connect(m_reviewFilter, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_filterModel->setReviewStatusFilter(index - 1);
        applyFilters();
    });

    detailLayout->addWidget(new QLabel(tr("Text Difference"), detailPanel));
    m_diffBrowser = new QTextBrowser(detailPanel);
    m_diffBrowser->setOpenExternalLinks(false);
    detailLayout->addWidget(m_diffBrowser, 1);

    auto* previewSplitter = new QSplitter(Qt::Horizontal, detailPanel);
    auto* beforeGroup = new QGroupBox(m_labels.leftProjectTitle, previewSplitter);
    auto* beforeLayout = new QVBoxLayout(beforeGroup);
    m_beforeCanvas = new ImageCanvas(beforeGroup);
    m_beforeCanvas->setReadOnly(true);
    m_beforeCanvas->setPreferences(m_preferences);
    m_beforeCanvas->setGroups(m_beforeProject.groups());
    m_beforeCanvas->setVisibleGroups(m_beforeProject.groups());
    beforeLayout->addWidget(m_beforeCanvas);

    auto* afterGroup = new QGroupBox(m_labels.rightProjectTitle, previewSplitter);
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
    if (m_reviewPanel)
        rootLayout->addWidget(m_reviewPanel);

    connect(m_beforeCanvas, &ImageCanvas::viewportStateChanged, this,
            [this](int zoomPercent, QPointF normalizedCenter) {
                syncPreviewCanvases(m_beforeCanvas, zoomPercent, normalizedCenter);
            });
    connect(m_afterCanvas, &ImageCanvas::viewportStateChanged, this, [this](int zoomPercent, QPointF normalizedCenter) {
        syncPreviewCanvases(m_afterCanvas, zoomPercent, normalizedCenter);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_jumpButton = buttons->addButton(tr("Jump to Label"), QDialogButtonBox::AcceptRole);
    auto* exportButton = buttons->addButton(tr("Export HTML..."), QDialogButtonBox::ActionRole);
    connect(m_jumpButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(exportButton, &QPushButton::clicked, this, &ProofreadChangesDialog::exportReport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    rebuildFilters();
    applyFilters();
    m_table->setSortingEnabled(true);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                scheduleDetailsUpdate(current.row());
                updateJumpButton();
            });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.column() != ProofreadChangeTableModel::ReviewColumn && changeIndexForRow(index.row()) >= 0) {
            m_isClosing = true;
            QTimer::singleShot(0, this, &QDialog::accept);
        }
    });

    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (!m_reviewPanel || !m_sessionWritable || !m_reviewPanel->isEnabled() ||
            index.column() != ProofreadChangeTableModel::ReviewColumn)
            return;
        const auto* change = m_tableModel->changeAt(changeIndexForRow(index.row()));
        if (!change)
            return;
        const auto status = static_cast<labelqt::services::ReviewStatus>(
            index.data(ProofreadChangeTableModel::ReviewStatusRole).toInt());
        m_reviewPanel->setStatus({*change}, status == labelqt::services::ReviewStatus::Reviewed
                                                ? labelqt::services::ReviewStatus::Unseen
                                                : labelqt::services::ReviewStatus::Reviewed);
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (m_reviewPanel)
            m_reviewPanel->refreshSelection();
    });

    if (m_tableModel->rowCount() > 0) {
        m_table->setCurrentIndex(m_filterModel->index(0, 0));
    }
    resizeTableRowsToContents();
    updateDetails(m_table->currentIndex().row());
    updateJumpButton();
}

void ProofreadChangesDialog::rebuildFilters()
{
    auto installFilter = [this](CheckableFilterButton* button, QString allText, QString noneText, QString countText,
                                QVector<CheckableFilterOption> options) {
        button->setTexts(std::move(allText), std::move(noneText), std::move(countText));
        button->setOptions(std::move(options));
        connect(button, &CheckableFilterButton::selectionChanged, this, &ProofreadChangesDialog::applyFilters,
                Qt::UniqueConnection);
    };

    QVector<CheckableFilterOption> pageOptions;
    QSet<QString> seenPages;
    for (const labelqt::services::ReviewChange& change : m_tableModel->changes()) {
        const QString pageName = pageNameForReviewChange(change);
        if (pageName.isEmpty() || seenPages.contains(pageName)) {
            continue;
        }
        seenPages.insert(pageName);
        pageOptions.append({pageName, pageName});
    }

    installFilter(m_pageFilterButton, tr("All pages"), tr("No pages"), tr("%1 page(s)"), pageOptions);
    installFilter(m_kindFilterButton, tr("All changes"), tr("No changes"), tr("%1 change type(s)"),
                  {
                      {QString(addedKindKey), tr("Added")},
                      {QString(deletedKindKey), tr("Deleted")},
                      {QString(modifiedKindKey), tr("Modified")},
                      {QStringLiteral("comment"), tr("Comment only")},
                  });
    installFilter(
        m_summaryFilterButton, tr("All summaries"), tr("No summaries"), tr("%1 summary item(s)"),
        {
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Text),
             facetText(labelqt::services::ReviewChangeFacet::Text)},
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Format),
             facetText(labelqt::services::ReviewChangeFacet::Format)},
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Group),
             facetText(labelqt::services::ReviewChangeFacet::Group)},
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Marker),
             facetText(labelqt::services::ReviewChangeFacet::Marker)},
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Order),
             facetText(labelqt::services::ReviewChangeFacet::Order)},
            {labelqt::services::ReviewChangeClassifier::facetKey(labelqt::services::ReviewChangeFacet::Comment),
             facetText(labelqt::services::ReviewChangeFacet::Comment)},
        });
}

void ProofreadChangesDialog::applyFilters()
{
    const auto* pageFilter = m_pageFilterButton;
    const auto* kindFilter = m_kindFilterButton;
    const auto* summaryFilter = m_summaryFilterButton;
    if (pageFilter == nullptr || kindFilter == nullptr || summaryFilter == nullptr) {
        return;
    }

    const QSet<QString> selectedPages = pageFilter->selectedKeys();
    const QSet<QString> selectedKinds = kindFilter->selectedKeys();
    const QSet<QString> selectedSummaryFacets = summaryFilter->selectedKeys();
    const bool allSummariesSelected = summaryFilter->isAllSelected();

    m_filterModel->setFilters(selectedPages, selectedKinds, selectedSummaryFacets, allSummariesSelected);
    resizeTableRowsToContents();
    if (m_filterModel->rowCount() > 0) {
        m_table->setCurrentIndex(m_filterModel->index(0, 0));
    }
    else {
        updateDetails(-1);
        updateJumpButton();
    }
    m_summaryLabel->setText(
        tr("Showing %1 of %2 changes").arg(m_filterModel->rowCount()).arg(m_tableModel->rowCount()));
    if (m_reviewPanel)
        updateReviewAnnotations();
}

void ProofreadChangesDialog::restoreTableColumnWidths()
{
    if (m_table == nullptr) {
        return;
    }

    const QVector<int> widths = m_sessionStateStore.proofreadingChangesColumnWidths(defaultColumnWidths());
    for (int column = 0; column < widths.size() && column < m_filterModel->columnCount(); ++column) {
        m_table->setColumnWidth(column, widths.at(column));
    }
}

void ProofreadChangesDialog::saveTableColumnWidths() const
{
    if (m_table == nullptr) {
        return;
    }

    QVector<int> widths;
    widths.reserve(m_filterModel->columnCount());
    for (int column = 0; column < m_filterModel->columnCount(); ++column) {
        widths.append(m_table->columnWidth(column));
    }
    m_sessionStateStore.saveProofreadingChangesColumnWidths(widths);
}

void ProofreadChangesDialog::exportReport()
{
    const QVector<labelqt::services::ReviewChange> changesToExport = filteredChanges();
    if (changesToExport.isEmpty()) {
        QMessageBox::information(this, tr("Proofreading"), tr("No proofreading changes to export."));
        return;
    }

    QString defaultPath =
        m_sessionStateStore.lastFileDialogDirectory(labelqt::services::FileDialogScope::ExportProofreadingReport);
    const QString baseName = QFileInfo(m_currentProject.filePath()).completeBaseName();
    const QString fileName = baseName.isEmpty() ? QStringLiteral("proofreading-report.html")
                                                : QStringLiteral("%1-proofreading-report.html").arg(baseName);
    defaultPath = defaultPath.isEmpty() ? fileName : QDir(defaultPath).filePath(fileName);

    const QString path = QFileDialog::getSaveFileName(this, tr("Export proofreading report"), defaultPath,
                                                      tr("HTML files (*.html);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    m_sessionStateStore.saveLastFileDialogPath(labelqt::services::FileDialogScope::ExportProofreadingReport, path);

    labelqt::services::ProofreadReportOptions options;
    if (m_reviewPanel) {
        options.includeReview = true;
        options.annotations = m_reviewPanel->session().annotations;
    }
    const std::optional<std::expected<void, QString>> result = runProofreadReportExportTask(
        this, QString(), tr("Exporting proofreading report..."), tr("Abort"), path, changesToExport, m_beforeProject,
        m_currentProject, reportTexts(), reportFilterDescription(), options);
    if (!result.has_value()) {
        return;
    }
    if (!result->has_value()) {
        QMessageBox::warning(this, tr("Proofreading"),
                             tr("Failed to export proofreading report: %1").arg(result->error()));
        return;
    }

    QMessageBox::information(this, tr("Proofreading"), tr("Proofreading report exported."));
}

int ProofreadChangesDialog::changeIndexForRow(int row) const
{
    if (m_table == nullptr || m_filterModel == nullptr || row < 0 || row >= m_filterModel->rowCount()) {
        return -1;
    }
    const int changeIndex = m_filterModel->index(row, 0).data(ProofreadChangeTableModel::ChangeIndexRole).toInt();
    return changeIndex >= 0 && changeIndex < m_tableModel->rowCount() ? changeIndex : -1;
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
    if (m_reviewPanel)
        m_reviewPanel->setCurrentChange(m_tableModel->changeAt(changeIndex));
    if (m_diffBrowser == nullptr || changeIndex < 0) {
        if (m_diffBrowser != nullptr) {
            m_diffBrowser->clear();
        }
        if (m_beforeCanvas != nullptr) {
            m_beforeCanvas->setImage(QString(), {});
        }
        if (m_afterCanvas != nullptr) {
            m_afterCanvas->setImage(QString(), {});
        }
        return;
    }

    const labelqt::services::ReviewChange& change = m_tableModel->changes().at(changeIndex);
    const QString beforeText =
        change.kind == labelqt::services::ReviewChangeKind::Added ? QString() : change.baseline.text;
    const QString afterText =
        change.kind == labelqt::services::ReviewChangeKind::Deleted ? QString() : change.current.text;
    m_diffBrowser->setHtml(labelqt::services::TextDiffHtmlRenderer::renderInlineDiff(
        beforeText, afterText, tr("No text change."), change.kind == labelqt::services::ReviewChangeKind::Comment) +
        (change.noteText.isEmpty() ? QString() : QStringLiteral("<hr><div style=\"white-space:pre-wrap\">%1</div>")
                                                    .arg(change.noteText.toHtmlEscaped())));
    updatePreviewCanvases(change);
}

void ProofreadChangesDialog::resizeTableRowsToContents()
{
    if (m_table == nullptr) {
        return;
    }

    for (int row = 0; row < m_filterModel->rowCount(); ++row) {
        resizeTableRowToContent(row);
    }
}

void ProofreadChangesDialog::resizeTableRowToContent(int row)
{
    if (m_table == nullptr || m_filterModel == nullptr || row < 0 || row >= m_filterModel->rowCount()) {
        return;
    }

    const QFontMetrics metrics(m_table->font());
    const int maximumTextRows = std::max(1, m_preferences.labelTableMaxTextRows());
    const int contentHeight = metrics.lineSpacing() * maximumTextRows;
    const int maximumHeight =
        std::max(m_table->verticalHeader()->minimumSectionSize(), contentHeight + textCellVerticalMargin);

    int naturalHeight = metrics.lineSpacing() + textCellVerticalMargin;
    const QModelIndex summaryIndex = m_filterModel->index(row, ProofreadChangeTableModel::SummaryColumn);
    if (summaryIndex.isValid()) {
        const int textWidth = std::max(1, m_table->columnWidth(ProofreadChangeTableModel::SummaryColumn) -
                                              2 * m_table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin));
        naturalHeight =
            std::max(naturalHeight, wrappedPlainTextHeight(summaryIndex.data().toString(), m_table->font(), textWidth) +
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
    const labelqt::core::ImageEntry* beforeImage = m_beforeProject.imageByName(baselineImageName);
    const labelqt::core::ImageEntry* currentImage = m_currentProject.imageByName(change.imageName);
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
    m_afterCanvas->setImage(displayImage->path,
                            currentImage == nullptr ? QVector<labelqt::core::Label>{} : currentImage->labels);
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
    if (m_labels.roundReview && selectedChange() && !selectedChange()->jumpAvailable) {
        m_jumpButton->setEnabled(false);
        return;
    }

    const int imageIndex = selectedImageIndex();
    const int labelIndex = selectedLabelIndex();
    const labelqt::core::Project& jumpProject = m_labels.jumpToLeftProject ? m_beforeProject : m_currentProject;
    const bool canJumpToLabel = imageIndex >= 0 && imageIndex < jumpProject.images().size() && labelIndex >= 0 &&
                                labelIndex < jumpProject.images().at(imageIndex).labels.size() &&
                                !jumpProject.images().at(imageIndex).labels.at(labelIndex).isDeleted();
    const bool canJumpToPage = imageIndex >= 0 && imageIndex < jumpProject.images().size();
    m_jumpButton->setEnabled(canJumpToLabel || canJumpToPage);
}

QString ProofreadChangesDialog::facetText(labelqt::services::ReviewChangeFacet facet) const
{
    switch (facet) {
    case labelqt::services::ReviewChangeFacet::Comment:
        return tr("Comment");
    case labelqt::services::ReviewChangeFacet::Text:
        return tr("text");
    case labelqt::services::ReviewChangeFacet::Format:
        return tr("format");
    case labelqt::services::ReviewChangeFacet::Group:
        return tr("group");
    case labelqt::services::ReviewChangeFacet::Marker:
        return tr("marker");
    case labelqt::services::ReviewChangeFacet::Order:
        return tr("order");
    }
    return {};
}

QVector<labelqt::services::ReviewChange> ProofreadChangesDialog::filteredChanges() const
{
    QVector<labelqt::services::ReviewChange> changes;
    changes.reserve(m_filterModel == nullptr ? 0 : m_filterModel->rowCount());
    for (int row = 0; m_filterModel != nullptr && row < m_filterModel->rowCount(); ++row) {
        const int changeIndex = m_filterModel->index(row, 0).data(ProofreadChangeTableModel::ChangeIndexRole).toInt();
        if (changeIndex >= 0 && changeIndex < m_tableModel->rowCount()) {
            changes.append(m_tableModel->changes().at(changeIndex));
        }
    }
    return changes;
}

QString ProofreadChangesDialog::reportFilterDescription() const
{
    auto selectedText = [this](const CheckableFilterButton* button) {
        if (button == nullptr || button->isAllSelected()) {
            return button == nullptr ? QString() : button->text();
        }
        const QStringList texts = button->selectedTexts();
        return texts.isEmpty() ? button->text() : texts.join(QStringLiteral(", "));
    };

    QString description = tr("Pages: %1; Changes: %2; Summaries: %3")
                              .arg(selectedText(m_pageFilterButton), selectedText(m_kindFilterButton),
                                   selectedText(m_summaryFilterButton));
    if (m_labels.reviewEnabled)
        description += tr("; Review: %1").arg(m_reviewFilter->currentText());
    return description;
}

QJsonObject ProofreadChangesDialog::reviewSession() const
{
    if (!m_sessionWritable)
        return labelqt::services::ReviewSessionService::projectSession(m_currentProject);
    return m_reviewPanel ? labelqt::services::ReviewSessionService::encode(m_reviewPanel->session()) : QJsonObject{};
}

QVector<labelqt::services::ReviewChange> ProofreadChangesDialog::selectedChanges() const
{
    QVector<labelqt::services::ReviewChange> result;
    for (const auto& index : m_table->selectionModel()->selectedRows()) {
        const auto* change = m_tableModel->changeAt(changeIndexForRow(index.row()));
        if (change)
            result.append(*change);
    }
    return result;
}

void ProofreadChangesDialog::updateReviewAnnotations()
{
    if (!m_reviewPanel)
        return;
    QVector<labelqt::services::ReviewAnnotation> annotations;
    for (const auto& change : m_tableModel->changes())
        annotations.append(
            m_reviewPanel->session().annotations.value(labelqt::services::ReviewSessionService::fingerprint(change)));
    m_tableModel->setReviewAnnotations(std::move(annotations), {tr("Unseen"), tr("Reviewed"), tr("Needs confirmation")},
                                       tr("Manual pair"));
    int reviewed = 0;
    for (int row = 0; row < m_filterModel->rowCount(); ++row)
        if (m_filterModel->index(row, 0).data(ProofreadChangeTableModel::ReviewStatusRole).toInt() == 1)
            ++reviewed;
    m_summaryLabel->setText(tr("Showing %1 of %2 changes; reviewed: %3")
                                .arg(m_filterModel->rowCount())
                                .arg(m_tableModel->rowCount())
                                .arg(reviewed));
}

void ProofreadChangesDialog::updateManualPairs()
{
    if (!m_reviewPanel)
        return;
    const auto result = labelqt::services::ProjectComparisonService::changesWithPairs(
        m_currentProject, m_metadata, m_pagePairs, m_reviewPanel->session().pairs);
    if (!result)
        return;
    m_tableModel->setChanges(*result);
    updateReviewAnnotations();
    applyFilters();
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
        .before = m_labels.leftProjectTitle,
        .after = m_labels.rightProjectTitle,
        .added = tr("Added"),
        .deleted = tr("Deleted"),
        .modified = tr("Modified"),
        .text = tr("text"),
        .format = tr("format"),
        .group = tr("group"),
        .marker = tr("marker"),
        .order = tr("order"),
        .noTextChange = tr("No text change."),
        .filter = tr("Filters"),
        .review = tr("Review"),
        .manualPair = tr("Manual pair"),
        .reviewStates = {tr("Unseen"), tr("Reviewed"), tr("Needs confirmation")},
        .comment = tr("Comment"),
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
