#include "ui/project/ProjectMergeDialog.h"

#include "ui/project/MergeCandidatePreview.h"
#include "ui/widgets/DialogWindowUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QVBoxLayout>

ProjectMergeDialog::ProjectMergeDialog(labelqt::services::ProjectMergePlan mergePlan,
                                       labelqt::core::AppPreferences preferences, QWidget* parent)
    : QDialog(parent), m_mergePlan(std::move(mergePlan)), m_preferences(std::move(preferences))
{
    m_selectedCandidateIndexes.resize(m_mergePlan.conflicts.size());
    m_conflictViewStates.resize(m_mergePlan.conflicts.size());
    for (int i = 0; i < m_selectedCandidateIndexes.size(); ++i)
        m_selectedCandidateIndexes[i] = m_mergePlan.conflicts.at(i).selectedCandidateIndex;
    buildUi();
}

ProjectMergeDialog::~ProjectMergeDialog()
{
    stopPreviews();
    for (QObject* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
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
    if (m_openMergedProjectCheckBox != nullptr)
        m_openMergedProjectCheckBox->setChecked(shouldOpen);
}

void ProjectMergeDialog::done(int result)
{
    stopPreviews();
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
    m_conflictList->setObjectName(QStringLiteral("mergeConflicts"));
    m_conflictList->setMinimumWidth(160);
    auto* content = new QWidget(splitter);
    auto* contentLayout = new QVBoxLayout(content);
    m_conflictTitle = new QLabel(content);
    m_conflictTitle->setWordWrap(true);
    contentLayout->addWidget(m_conflictTitle);
    m_sourceChoice = new QComboBox(content);
    m_sourceChoice->setObjectName(QStringLiteral("mergeSourceChoice"));
    m_sourceChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sourceChoice->setMinimumContentsLength(12);
    m_conflictTitle->setBuddy(m_sourceChoice);
    contentLayout->addWidget(m_sourceChoice);

    auto* previewsLayout = new QHBoxLayout;
    for (std::size_t slot = 0; slot < 2; ++slot) {
        auto* column = new QVBoxLayout;
        auto* choice = new QComboBox(content);
        choice->setObjectName(QStringLiteral("mergePreviewChoice%1").arg(slot));
        choice->setToolTip(tr("Preview candidate"));
        choice->setAccessibleName(tr("Preview candidate"));
        choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        choice->setMinimumContentsLength(8);
        m_candidateChoices[slot] = choice;
        column->addWidget(choice);
        auto* preview = new MergeCandidatePreview(m_preferences, m_mergePlan.mergedProject.groups(), content);
        m_previews[slot] = preview;
        column->addWidget(preview, 1);
        previewsLayout->addLayout(column, 1);
        connect(choice, &QComboBox::currentIndexChanged, this,
                [this, slot](int candidateIndex) { displayCandidate(slot, candidateIndex); });
        connect(preview, &MergeCandidatePreview::viewportStateChanged, this,
                [this, slot](int zoom, QPointF center) { syncCandidateCanvasViews(slot, zoom, center); });
    }
    contentLayout->addLayout(previewsLayout, 1);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 850});
    rootLayout->addWidget(splitter, 1);

    populateConflictList();
    connect(m_conflictList, &QListWidget::currentRowChanged, this, &ProjectMergeDialog::displayConflict);
    connect(m_sourceChoice, &QComboBox::currentIndexChanged, this,
            [this](int candidateIndex) { selectCandidate(m_currentConflict, candidateIndex); });
    if (m_conflictList->count() > 0)
        m_conflictList->setCurrentRow(0);
    else
        displayConflict(-1);

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
    for (const auto& conflict : std::as_const(m_mergePlan.conflicts)) {
        auto* item = new QListWidgetItem(
            tr("%1 (%n candidate(s))", nullptr, static_cast<int>(conflict.candidates.size())).arg(conflict.imageName));
        item->setToolTip(conflict.imageName);
        m_conflictList->addItem(item);
    }
}

void ProjectMergeDialog::displayConflict(int conflictIndex)
{
    if (m_isClosing)
        return;
    {
        const QScopedValueRollback updating(m_isUpdating, true);
        m_currentConflict = conflictIndex;
        m_sourceChoice->clear();
        for (std::size_t slot = 0; slot < 2; ++slot) {
            m_previews[slot]->clear();
            m_candidateChoices[slot]->clear();
        }
        const bool valid = conflictIndex >= 0 && conflictIndex < m_mergePlan.conflicts.size();
        m_sourceChoice->setEnabled(valid);
        m_conflictTitle->clear();
        if (!valid)
            return;

        const auto& conflict = m_mergePlan.conflicts.at(conflictIndex);
        m_conflictTitle->setText(tr("Choose the project to use for %1.").arg(conflict.imageName));
        for (const auto& candidate : conflict.candidates) {
            const QString title = tr("%1 - %n label(s)", nullptr, candidate.labelCount).arg(candidate.projectPath);
            for (QComboBox* choice : {m_sourceChoice, m_candidateChoices[0], m_candidateChoices[1]}) {
                choice->addItem(title);
                choice->setItemData(choice->count() - 1, candidate.projectPath, Qt::ToolTipRole);
            }
        }
        const int selected = m_selectedCandidateIndexes.at(conflictIndex);
        m_sourceChoice->setCurrentIndex(selected);
        auto& state = m_conflictViewStates[conflictIndex];
        if (state.candidates[0] < 0) {
            state.candidates[0] = selected >= 0 && selected < conflict.candidates.size() ? selected : 0;
            state.candidates[1] = conflict.candidates.size() > 1 ? (state.candidates[0] == 0 ? 1 : 0) : -1;
        }
        for (std::size_t slot = 0; slot < 2; ++slot) {
            m_candidateChoices[slot]->setCurrentIndex(state.candidates[slot]);
            m_candidateChoices[slot]->setVisible(slot == 0 || conflict.candidates.size() > 1);
            m_previews[slot]->setVisible(slot == 0 || conflict.candidates.size() > 1);
            m_previews[slot]->restoreView(state.zoomPercent, state.normalizedCenter);
        }
    }
    for (std::size_t slot = 0; slot < 2; ++slot)
        displayCandidate(slot, m_candidateChoices[slot]->currentIndex());
}

void ProjectMergeDialog::displayCandidate(std::size_t slot, int candidateIndex)
{
    if (m_isUpdating || m_isClosing || m_currentConflict < 0 || m_currentConflict >= m_mergePlan.conflicts.size())
        return;
    const auto& candidates = m_mergePlan.conflicts.at(m_currentConflict).candidates;
    auto& state = m_conflictViewStates[m_currentConflict];
    state.candidates[slot] = candidateIndex;
    if (candidateIndex < 0 || candidateIndex >= candidates.size()) {
        m_previews[slot]->clear();
        return;
    }
    m_previews[slot]->restoreView(state.zoomPercent, state.normalizedCenter);
    m_previews[slot]->setCandidate(candidates.at(candidateIndex).image);
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
    if (m_isUpdating || m_isClosing || conflictIndex < 0 || conflictIndex >= m_selectedCandidateIndexes.size() ||
        candidateIndex < 0 || candidateIndex >= m_mergePlan.conflicts.at(conflictIndex).candidates.size())
        return;
    m_selectedCandidateIndexes[conflictIndex] = candidateIndex;
    m_candidateChoices[0]->setCurrentIndex(candidateIndex);
}

void ProjectMergeDialog::syncCandidateCanvasViews(std::size_t sourceSlot, int zoomPercent, QPointF normalizedCenter)
{
    if (m_isUpdating || m_isClosing || m_currentConflict < 0 || m_currentConflict >= m_conflictViewStates.size())
        return;
    const QScopedValueRollback updating(m_isUpdating, true);
    auto& state = m_conflictViewStates[m_currentConflict];
    state.zoomPercent = zoomPercent;
    state.normalizedCenter = normalizedCenter;
    m_previews[1 - sourceSlot]->restoreView(zoomPercent, normalizedCenter);
}

void ProjectMergeDialog::stopPreviews()
{
    m_isClosing = true;
    for (auto& preview : m_previews) {
        if (preview) {
            disconnect(preview, nullptr, this, nullptr);
            preview->stop();
        }
        preview.clear();
    }
}
