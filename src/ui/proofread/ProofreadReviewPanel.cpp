#include "ui/proofread/ProofreadReviewPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace labelqt::services;

ProofreadReviewPanel::ProofreadReviewPanel(Callbacks callbacks, QWidget* parent)
    : QWidget(parent), m_callbacks(std::move(callbacks))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    m_status = new QComboBox(this);
    m_status->addItems({tr("Unseen"), tr("Reviewed"), tr("Needs confirmation")});
    m_status->setToolTip(tr("Set status for selected changes"));
    auto* next = new QPushButton(tr("Next unseen"), this);
    auto* pair = new QPushButton(tr("Pair with..."), this);
    auto* unpair = new QPushButton(tr("Remove manual pair"), this);
    auto* undo = new QToolButton(this);
    auto* redo = new QToolButton(this);
    undo->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    redo->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    undo->setToolTip(tr("Undo review edit"));
    redo->setToolTip(tr("Redo review edit"));
    bar->addWidget(m_status);
    bar->addWidget(next);
    bar->addWidget(pair);
    bar->addWidget(unpair);
    bar->addWidget(undo);
    bar->addWidget(redo);
    bar->addStretch();
    layout->addLayout(bar);
    auto* notes = new QCheckBox(tr("Review note"), this);
    layout->addWidget(notes);
    auto* history = new QPushButton(tr("Previous notes..."), this);
    bar->addWidget(history);
    connect(history, &QPushButton::clicked, this, [this] {
        commitNote();
        QSet<QString> active;
        for (const auto& change : m_callbacks.allChanges())
            active.insert(ReviewSessionService::fingerprint(change));
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Previous notes"));
        dialog.resize(640, 400);
        QVBoxLayout historyLayout(&dialog);
        QTableWidget table(0, 2, &dialog);
        table.setHorizontalHeaderLabels({tr("Change fingerprint"), tr("Review note")});
        table.setEditTriggers(QAbstractItemView::NoEditTriggers);
        table.horizontalHeader()->setStretchLastSection(true);
        for (auto it = m_session.annotations.cbegin(); it != m_session.annotations.cend(); ++it) {
            if (active.contains(it.key()) || it->note.isEmpty())
                continue;
            const int row = table.rowCount();
            table.insertRow(row);
            table.setItem(row, 0, new QTableWidgetItem(it.key()));
            table.setItem(row, 1, new QTableWidgetItem(it->note));
        }
        historyLayout.addWidget(&table);
        QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
        historyLayout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    });
    m_note = new QPlainTextEdit(this);
    m_note->setMaximumHeight(90);
    m_note->setTabChangesFocus(true);
    auto* apply = new QPushButton(tr("Apply note"), this);
    layout->addWidget(m_note);
    layout->addWidget(apply, 0, Qt::AlignRight);
    m_note->hide();
    apply->hide();
    connect(notes, &QCheckBox::toggled, m_note, &QWidget::setVisible);
    connect(notes, &QCheckBox::toggled, apply, &QWidget::setVisible);
    connect(apply, &QPushButton::clicked, this, &ProofreadReviewPanel::commitNote);
    connect(next, &QPushButton::clicked, this, &ProofreadReviewPanel::nextUnseenRequested);
    connect(pair, &QPushButton::clicked, this, &ProofreadReviewPanel::pairSelected);
    connect(unpair, &QPushButton::clicked, this, &ProofreadReviewPanel::unpairSelected);
    connect(m_status, &QComboBox::activated, this, [this](int status) {
        commitNote();
        auto session = m_session;
        for (const auto& change : m_callbacks.selected())
            session.annotations[ReviewSessionService::fingerprint(change)].status = static_cast<ReviewStatus>(status);
        changeState(std::move(session), false);
    });
    connect(undo, &QToolButton::clicked, this, [this] {
        commitNote();
        m_undo.undo();
    });
    connect(redo, &QToolButton::clicked, this, [this] { m_undo.redo(); });
    m_undo.setChangedCallback([this, undo, redo] {
        undo->setEnabled(m_undo.canUndo());
        redo->setEnabled(m_undo.canRedo());
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
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
    commitNote();
    m_currentKey = nextKey;
    refreshCurrentAnnotation();
}
void ProofreadReviewPanel::refreshCurrentAnnotation()
{
    const auto annotation = m_session.annotations.value(m_currentKey);
    m_note->setPlainText(annotation.note);
    m_note->setEnabled(!m_currentKey.isEmpty());
    m_status->setEnabled(!m_currentKey.isEmpty());
    const QSignalBlocker blocker(m_status);
    m_status->setCurrentIndex(static_cast<int>(annotation.status));
}
void ProofreadReviewPanel::commitNote()
{
    if (m_currentKey.isEmpty() || m_session.annotations.value(m_currentKey).note == m_note->toPlainText())
        return;
    auto session = m_session;
    session.annotations[m_currentKey].note = m_note->toPlainText();
    changeState(std::move(session), false);
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
    commitNote();
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
    commitNote();
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
