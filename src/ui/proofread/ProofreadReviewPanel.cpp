#include "ui/proofread/ProofreadReviewPanel.h"
#include "ui/proofread/ReviewStatusDelegate.h"
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <algorithm>

using namespace labelqt::services;

ProofreadReviewPanel::ProofreadReviewPanel(Callbacks callbacks, QWidget* parent, bool allowManualPairs)
    : QWidget(parent), m_callbacks(std::move(callbacks))
{
    auto* bar = new QHBoxLayout(this);
    bar->setContentsMargins(0, 0, 0, 0);
    m_status = new QComboBox(this);
    m_status->setObjectName(QStringLiteral("reviewStatus"));
    m_status->addItems({tr("Unseen"), tr("Reviewed"), tr("Needs confirmation")});
    for (int i = 0; i < m_status->count(); ++i)
        m_status->setItemIcon(i, labelqt::ui::reviewStatusIcon(static_cast<ReviewStatus>(i), *style()));
    m_status->setToolTip(tr("Set status for selected changes"));
    auto* next = new QPushButton(tr("Next unseen"), this);
    next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    auto* tools = new QToolButton(this);
    tools->setIcon(
        QIcon::fromTheme(QStringLiteral("insert-link"), style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    tools->setToolTip(tr("Manual pair"));
    tools->setAccessibleName(tools->toolTip());
    tools->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(tools);
    m_pairAction = menu->addAction(tr("Pair with..."), this, &ProofreadReviewPanel::pairSelected);
    m_unpairAction = menu->addAction(tr("Remove manual pair"), this, &ProofreadReviewPanel::unpairSelected);
    tools->setMenu(menu);
    tools->setVisible(allowManualPairs);
    auto* undo = new QToolButton(this);
    auto* redo = new QToolButton(this);
    undo->setObjectName(QStringLiteral("undoReview"));
    redo->setObjectName(QStringLiteral("redoReview"));
    undo->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo"), style()->standardIcon(QStyle::SP_ArrowBack)));
    redo->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo"), style()->standardIcon(QStyle::SP_ArrowForward)));
    undo->setToolTip(tr("Undo review edit"));
    redo->setToolTip(tr("Redo review edit"));
    bar->addWidget(m_status);
    bar->addWidget(next);
    bar->addStretch();
    bar->addWidget(tools);
    bar->addWidget(undo);
    bar->addWidget(redo);
    connect(next, &QPushButton::clicked, this, &ProofreadReviewPanel::nextUnseenRequested);
    connect(m_status, &QComboBox::activated, this,
            [this](int status) { setStatus(m_callbacks.selected(), static_cast<ReviewStatus>(status)); });
    connect(undo, &QToolButton::clicked, this, [this] { m_undo.undo(); });
    connect(redo, &QToolButton::clicked, this, [this] { m_undo.redo(); });
    m_undo.setChangedCallback([this, undo, redo] {
        undo->setEnabled(m_undo.canUndo());
        redo->setEnabled(m_undo.canRedo());
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
    refreshSelection();
}
ProofreadReviewPanel::~ProofreadReviewPanel()
{
    m_undo.setChangedCallback({});
    for (QObject* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
}
void ProofreadReviewPanel::setSession(ReviewSession session)
{
    m_session = std::move(session);
    m_undo.clear();
    refreshCurrentAnnotation();
}
const ReviewSession& ProofreadReviewPanel::session() const
{
    return m_session;
}
void ProofreadReviewPanel::setCurrentChange(const ReviewChange* change)
{
    const QString nextKey = change ? ReviewSessionService::fingerprint(*change) : QString();
    if (nextKey == m_currentKey)
        return;
    m_currentKey = nextKey;
    refreshCurrentAnnotation();
}
void ProofreadReviewPanel::refreshCurrentAnnotation()
{
    const auto annotation = m_session.annotations.value(m_currentKey);
    refreshSelection();
    const QSignalBlocker blocker(m_status);
    m_status->setCurrentIndex(static_cast<int>(annotation.status));
}
void ProofreadReviewPanel::setStatus(const QVector<ReviewChange>& changes, ReviewStatus status)
{
    if (status != ReviewStatus::Unseen && status != ReviewStatus::Reviewed && status != ReviewStatus::NeedsConfirmation)
        return;
    auto session = m_session;
    for (const auto& change : changes) {
        const auto key = ReviewSessionService::fingerprint(change);
        if (session.annotations.value(key).status != status)
            session.annotations[key].status = status;
    }
    changeState(std::move(session), false);
}

void ProofreadReviewPanel::refreshSelection()
{
    const auto selected = m_callbacks.selected();
    m_status->setEnabled(!selected.isEmpty());
    m_pairAction->setEnabled(selected.size() == 1 && selected.first().kind != ReviewChangeKind::Modified);
    m_unpairAction->setEnabled(
        std::ranges::any_of(selected, [](const auto& change) { return change.manuallyMatched; }));
}

void ProofreadReviewPanel::applyState(ReviewSession session, bool pairs)
{
    m_session = std::move(session);
    refreshCurrentAnnotation();
    if (pairs)
        emit pairsChanged();
    emit annotationsChanged();
}
void ProofreadReviewPanel::changeState(ReviewSession session, bool pairs)
{
    const auto validSession = ReviewSessionService::validate(session);
    if (!validSession) {
        QMessageBox::warning(this, tr("Edit review session"), validSession.error());
        return;
    }
    if (ReviewSessionService::encode(session) == ReviewSessionService::encode(m_session))
        return;
    if (pairs) {
        const auto valid = m_callbacks.validatePairs(session.pairs);
        if (!valid) {
            QMessageBox::warning(this, tr("Manual pair"), valid.error());
            return;
        }
    }
    const auto before = m_session;
    applyState(session, pairs);
    m_undo.push(
        tr("Edit review session"), [this, before, pairs] { applyState(before, pairs); },
        [this, session, pairs] { applyState(session, pairs); });
}
void ProofreadReviewPanel::pairSelected()
{
    const auto selected = m_callbacks.selected();
    if (selected.size() != 1 || selected.first().kind == ReviewChangeKind::Modified) {
        QMessageBox::information(this, tr("Manual pair"), tr("Select one added or deleted change."));
        return;
    }
    const auto source = selected.first();
    QVector<ReviewChange> candidates;
    QStringList labels;
    for (const auto& change : m_callbacks.allChanges()) {
        if (change.kind == ReviewChangeKind::Modified || change.kind == source.kind ||
            change.imageName != source.imageName || change.baselineImageName != source.baselineImageName)
            continue;
        candidates.append(change);
        const auto snapshot = change.kind == ReviewChangeKind::Added ? change.current : change.baseline;
        labels.append(QStringLiteral("#%1 (%2, %3): %4")
                          .arg(snapshot.labelIndex + 1)
                          .arg(snapshot.position.x(), 0, 'f', 3)
                          .arg(snapshot.position.y(), 0, 'f', 3)
                          .arg(snapshot.text));
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Manual pair"), tr("No unmatched labels on the other side of this page."));
        return;
    }
    bool ok = false;
    const auto label = QInputDialog::getItem(this, tr("Manual pair"), tr("Other-side label"), labels, 0, false, &ok);
    if (!ok)
        return;
    const auto target = candidates.at(labels.indexOf(label));
    auto session = m_session;
    session.pairs.append(source.kind == ReviewChangeKind::Deleted ? ManualLabelPair{source.baseline, target.current}
                                                                  : ManualLabelPair{target.baseline, source.current});
    changeState(std::move(session), true);
}
void ProofreadReviewPanel::unpairSelected()
{
    auto session = m_session;
    const auto selected = m_callbacks.selected();
    session.pairs.removeIf([&](const auto& pair) {
        return std::any_of(selected.begin(), selected.end(), [&](const auto& change) {
            return change.manuallyMatched && change.baselineLabelIndex == pair.baseline.labelIndex &&
                   change.currentLabelIndex == pair.current.labelIndex && change.imageName == pair.current.imageName &&
                   change.baselineImageName == pair.baseline.imageName;
        });
    });
    changeState(std::move(session), true);
}
