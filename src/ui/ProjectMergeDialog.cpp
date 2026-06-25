#include "ui/ProjectMergeDialog.h"

#include "ui/DialogWindowUtils.h"
#include "ui/ImageCanvas.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

ProjectMergeDialog::ProjectMergeDialog(labelqt::services::ProjectMergePlan mergePlan,
                                       labelqt::core::AppPreferences preferences, QWidget* parent)
    : QDialog(parent), m_mergePlan(std::move(mergePlan)), m_preferences(std::move(preferences))
{
    m_selectedCandidateIndexes.resize(m_mergePlan.conflicts.size());
    m_candidateCanvases.resize(m_mergePlan.conflicts.size());
    for (int i = 0; i < m_selectedCandidateIndexes.size(); ++i) {
        m_selectedCandidateIndexes[i] = m_mergePlan.conflicts.at(i).selectedCandidateIndex;
    }
    buildUi();
}

QVector<int> ProjectMergeDialog::selectedCandidateIndexes() const
{
    return m_selectedCandidateIndexes;
}

bool ProjectMergeDialog::shouldOpenMergedProjectAfterSave() const noexcept
{
    return m_openMergedProjectCheckBox == nullptr || m_openMergedProjectCheckBox->isChecked();
}

void ProjectMergeDialog::setShouldOpenMergedProjectAfterSave(bool shouldOpen)
{
    if (m_openMergedProjectCheckBox != nullptr) {
        m_openMergedProjectCheckBox->setChecked(shouldOpen);
    }
}

void ProjectMergeDialog::done(int result)
{
    m_isClosing = true;
    disconnectCandidateCanvases();
    QDialog::done(result);
}

void ProjectMergeDialog::buildUi()
{
    setWindowTitle(tr("Merge Projects"));
    labelqt::ui::configureLargeDialogWindow(*this, QSize(1100, 760));

    auto* rootLayout = new QVBoxLayout(this);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    rootLayout->addWidget(m_summaryLabel);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_conflictList = new QListWidget(splitter);
    m_conflictList->setMinimumWidth(220);
    m_conflictStack = new QStackedWidget(splitter);
    splitter->addWidget(m_conflictList);
    splitter->addWidget(m_conflictStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    populateConflictList();
    for (int i = 0; i < m_mergePlan.conflicts.size(); ++i) {
        m_conflictStack->addWidget(createConflictPage(i));
    }

    connect(m_conflictList, &QListWidget::currentRowChanged, m_conflictStack, &QStackedWidget::setCurrentIndex);
    if (m_conflictList->count() > 0) {
        m_conflictList->setCurrentRow(0);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_openMergedProjectCheckBox = new QCheckBox(tr("Open merged project after saving"), this);
    buttons->addButton(m_openMergedProjectCheckBox, QDialogButtonBox::ActionRole);
    rootLayout->addWidget(buttons);

    updateSummary();
}

void ProjectMergeDialog::populateConflictList()
{
    for (int i = 0; i < m_mergePlan.conflicts.size(); ++i) {
        const labelqt::services::ProjectMergeConflict& conflict = m_mergePlan.conflicts.at(i);
        auto* item = new QListWidgetItem(
            tr("%1 (%n candidate(s))", nullptr, static_cast<int>(conflict.candidates.size())).arg(conflict.imageName));
        item->setToolTip(conflict.imageName);
        m_conflictList->addItem(item);
    }
}

QWidget* ProjectMergeDialog::createConflictPage(int conflictIndex)
{
    const labelqt::services::ProjectMergeConflict& conflict = m_mergePlan.conflicts.at(conflictIndex);

    auto* page = new QWidget(m_conflictStack);
    auto* layout = new QVBoxLayout(page);
    auto* title = new QLabel(tr("Choose the project to use for %1.").arg(conflict.imageName), page);
    title->setWordWrap(true);
    layout->addWidget(title);

    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    auto* content = new QWidget(scrollArea);
    auto* contentLayout = new QHBoxLayout(content);
    auto* buttonGroup = new QButtonGroup(content);
    buttonGroup->setExclusive(true);

    for (int candidateIndex = 0; candidateIndex < conflict.candidates.size(); ++candidateIndex) {
        QWidget* candidateWidget = createCandidateWidget(conflictIndex, candidateIndex);
        auto* radioButton = candidateWidget->findChild<QRadioButton*>();
        if (radioButton != nullptr) {
            buttonGroup->addButton(radioButton, candidateIndex);
            radioButton->setChecked(candidateIndex == m_selectedCandidateIndexes.at(conflictIndex));
        }
        contentLayout->addWidget(candidateWidget);
    }
    contentLayout->addStretch();

    connect(buttonGroup, &QButtonGroup::idClicked, this,
            [this, conflictIndex](int candidateIndex) { selectCandidate(conflictIndex, candidateIndex); });

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea, 1);
    return page;
}

QWidget* ProjectMergeDialog::createCandidateWidget(int conflictIndex, int candidateIndex)
{
    const labelqt::services::ProjectMergeCandidate& candidate =
        m_mergePlan.conflicts.at(conflictIndex).candidates.at(candidateIndex);

    auto* groupBox = new QGroupBox(this);
    groupBox->setMinimumWidth(640);
    auto* layout = new QVBoxLayout(groupBox);

    auto* radioButton = new QRadioButton(
        tr("%1 - %n label(s)", nullptr, candidate.labelCount).arg(QFileInfo(candidate.projectPath).fileName()),
        groupBox);
    radioButton->setToolTip(candidate.projectPath);
    layout->addWidget(radioButton);

    auto* pathLabel = new QLabel(candidate.projectPath, groupBox);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);

    auto* canvas = new ImageCanvas(groupBox);
    canvas->setMinimumHeight(360);
    canvas->setMinimumWidth(600);
    canvas->setReadOnly(true);
    canvas->setPreferences(m_preferences);
    canvas->setGroups(m_mergePlan.mergedProject.groups());
    canvas->setVisibleGroups(m_mergePlan.mergedProject.groups());
    canvas->setImage(candidate.image.path, candidate.image.labels);
    if (conflictIndex >= 0 && conflictIndex < m_candidateCanvases.size()) {
        m_candidateCanvases[conflictIndex].append(canvas);
    }
    connect(canvas, &ImageCanvas::viewportStateChanged, this,
            [this, conflictIndex, canvas](int zoomPercent, QPointF normalizedCenter) {
                syncCandidateCanvasViews(conflictIndex, canvas, zoomPercent, normalizedCenter);
            });
    layout->addWidget(canvas, 1);

    return groupBox;
}

void ProjectMergeDialog::updateSummary()
{
    const int pageCount = static_cast<int>(m_mergePlan.mergedProject.images().size());
    const int conflictCount = static_cast<int>(m_mergePlan.conflicts.size());
    m_summaryLabel->setText(
        tr("Loaded %n page(s). %1 page(s) need a source selection.", nullptr, pageCount).arg(conflictCount));
}

void ProjectMergeDialog::selectCandidate(int conflictIndex, int candidateIndex)
{
    if (conflictIndex < 0 || conflictIndex >= m_selectedCandidateIndexes.size()) {
        return;
    }
    m_selectedCandidateIndexes[conflictIndex] = candidateIndex;
}

void ProjectMergeDialog::syncCandidateCanvasViews(int conflictIndex, ImageCanvas* sourceCanvas, int zoomPercent,
                                                  QPointF normalizedCenter)
{
    if (m_isSyncingCanvasViews || sourceCanvas == nullptr || conflictIndex < 0 ||
        conflictIndex >= m_candidateCanvases.size() || m_isClosing) {
        return;
    }

    m_isSyncingCanvasViews = true;
    for (const QPointer<ImageCanvas>& canvas : m_candidateCanvases.at(conflictIndex)) {
        if (!canvas.isNull() && canvas != sourceCanvas) {
            canvas->restoreView(zoomPercent, normalizedCenter);
        }
    }
    m_isSyncingCanvasViews = false;
}

void ProjectMergeDialog::disconnectCandidateCanvases()
{
    for (const QVector<QPointer<ImageCanvas>>& canvases : std::as_const(m_candidateCanvases)) {
        for (const QPointer<ImageCanvas>& canvas : canvases) {
            if (!canvas.isNull()) {
                disconnect(canvas, nullptr, this, nullptr);
            }
        }
    }
}
