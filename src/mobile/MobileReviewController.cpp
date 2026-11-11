#include "mobile/MobileReviewController.h"

#include "services/proofread/ReviewChangeClassifier.h"
#include "services/proofread/TextDiffHtmlRenderer.h"

#include <QtConcurrentRun>

using namespace labelqt::services;

MobileReviewController::MobileReviewController(ProjectController& controller, labelqt::core::UndoStack& undo,
                                               QObject* parent)
    : QObject(parent), m_controller(controller), m_undo(undo)
{
    Q_ASSERT(controller.isOwnerThread() && thread() == controller.thread());
    connect(&controller, &ProjectController::storageChanged, this, &MobileReviewController::refresh);
    refresh();
}

MobileReviewController::~MobileReviewController()
{
    disconnect(&m_watcher, nullptr, this, nullptr);
}

void MobileReviewController::setError(QString error)
{
    if (m_error == error)
        return;
    m_error = std::move(error);
    emit errorChanged();
}

void MobileReviewController::refresh()
{
    try {
        const auto version = ReviewRoundEditService::token(m_controller);
        if (m_documentSession != m_controller.documentSession() || m_version.session != version.session ||
            m_version.revision != version.revision)
            m_changes.clear();
        m_documentSession = m_controller.documentSession();
        m_version = version;
        const auto round = ReviewRoundService::read(m_controller.project());
        m_notes.clear();
        m_roundState = !round            ? QStringLiteral("invalid")
                       : !*round         ? QStringLiteral("none")
                       : (**round).after ? QStringLiteral("completed")
                                         : QStringLiteral("active");
        if (round && *round)
            for (const auto& note : (**round).notes) {
                const auto target = ReviewRoundService::locateNote(m_controller.project(), note);
                const auto pageName =
                    target.first >= 0 ? m_controller.project().images().at(target.first).name : note.pageName;
                m_notes.append(QVariantMap{{"key", note.id.toString(QUuid::WithoutBraces)},
                                           {"text", note.text},
                                           {"resolved", note.resolved},
                                           {"pageName", pageName}});
            }
        if (!round || !*round)
            m_changes.clear();
        setError(round ? QString() : round.error());
    }
    catch (...) {
        m_roundState = QStringLiteral("invalid");
        m_changes.clear();
        m_notes.clear();
        setError(tr("Unable to read the proofreading round."));
    }
    emit changed();
}

bool MobileReviewController::edit(const QString& description, const std::function<QString(Round&)>& mutate)
{
    refresh();
    if (m_roundState == QStringLiteral("invalid"))
        return false;
    try {
        const auto token = ReviewRoundEditService::token(m_controller);
        auto round = ReviewRoundService::read(m_controller.project());
        if (!round) {
            setError(round.error());
            return false;
        }
        const auto error = mutate(*round);
        if (!error.isEmpty()) {
            setError(error);
            return false;
        }
        const auto result = ReviewRoundEditService::commit(m_controller, m_undo, token, *round, description);
        if (!result) {
            setError(result.error());
            return false;
        }
        refresh();
        emit edited();
        return true;
    }
    catch (...) {
        setError(tr("Unable to edit the proofreading round."));
        return false;
    }
}

void MobileReviewController::startRound(bool replaceCompleted)
{
    edit(tr("Start proofreading round"), [this, replaceCompleted](Round& round) -> QString {
        if (round && (!round->after || !replaceCompleted))
            return tr("A proofreading round already exists.");
        round.emplace();
        round->before = ReviewRoundService::snapshot(m_controller.project());
        return {};
    });
}

void MobileReviewController::completeRound()
{
    edit(tr("Complete proofreading round"), [this](Round& round) -> QString {
        if (!round || round->after)
            return tr("An active proofreading round is required.");
        *round = ReviewRoundService::complete(std::move(*round), m_controller.project());
        return {};
    });
}

bool MobileReviewController::addNote(QString pageId, QString labelId, QString text)
{
    return edit(tr("Add proofreading note"), [&](Round& round) -> QString {
        if (!round || round->after)
            return tr("An active proofreading round is required.");
        const QUuid page(pageId), label(labelId);
        const auto& project = m_controller.project();
        const auto target = ReviewRoundService::locate(project, page, label);
        if (page.isNull() || (!labelId.isEmpty() && label.isNull()) || target.first < 0 ||
            (!label.isNull() && target.second < 0))
            return tr("The note target no longer exists.");
        if (text.trimmed().isEmpty())
            return tr("The note text must not be empty.");
        ProofreadNote note;
        note.pageId = page;
        note.labelId = label;
        note.pageName = project.images().at(target.first).name;
        if (target.second >= 0)
            note.quote = project.images().at(target.first).labels.at(target.second).text();
        note.text = text;
        round->notes.append(std::move(note));
        return {};
    });
}

void MobileReviewController::resolveNote(QString noteId, bool resolved)
{
    edit(tr("Resolve proofreading note"), [&](Round& round) -> QString {
        if (round && !QUuid(noteId).isNull())
            for (auto& note : round->notes)
                if (note.id == QUuid(noteId)) {
                    note.resolved = resolved;
                    return {};
                }
        return tr("The proofreading note no longer exists.");
    });
}

void MobileReviewController::calculateChanges()
{
    if (m_busy)
        return;
    refresh();
    if (m_roundState == QStringLiteral("invalid"))
        return;
    try {
        const auto token = ReviewRoundEditService::token(m_controller);
        const auto session = m_controller.documentSession();
        const auto round = ReviewRoundService::read(m_controller.project());
        if (!round || !*round) {
            setError(!round ? round.error() : tr("No proofreading round exists."));
            return;
        }
        auto live = ReviewRoundService::snapshot(m_controller.project());
        disconnect(&m_watcher, nullptr, this, nullptr);
        connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this, token, session] {
            try {
                const auto current = ReviewRoundEditService::token(m_controller);
                if (session != m_controller.documentSession() || token.session != current.session ||
                    token.revision != current.revision) {
                    refresh();
                    setError(tr("The project changed. Calculate proofreading changes again."));
                }
                else {
                    const auto result = m_watcher.result();
                    m_changes = result ? *result : QVariantList();
                    setError(result ? QString() : result.error());
                    emit changed();
                }
            }
            catch (...) {
                m_changes.clear();
                setError(tr("Unable to calculate proofreading changes."));
                emit changed();
            }
            m_busy = false;
            emit busyChanged();
        });
        const auto future =
            QtConcurrent::run([round = **round, live = std::move(live), noChange = tr("No text changes"),
                               tooLarge = tr("This round is too large for the mobile preview."),
                               failure = tr("Unable to calculate proofreading changes.")]() -> Result {
                try {
                    const auto presentation = ReviewRoundService::present(round, live);
                    // Reject the entire preview, never silently omit rows or shorten text.
                    if (presentation.changes.size() > 1000)
                        return std::unexpected(tooLarge);
                    QVariantList rows;
                    qsizetype characters = 0, renderedCharacters = 0;
                    for (const auto& change : presentation.changes) {
                        characters += change.baseline.text.size() + change.current.text.size() + change.noteText.size();
                        if (characters > 1024 * 1024)
                            return std::unexpected(tooLarge);
                        const auto html =
                            TextDiffHtmlRenderer::renderInlineDiff(change.baseline.text, change.current.text, noChange);
                        renderedCharacters += html.size();
                        if (renderedCharacters > 8 * 1024 * 1024)
                            return std::unexpected(tooLarge);
                        rows.append(QVariantMap{{"pageName", change.pageName()},
                                                {"number", change.labelIndex + 1},
                                                {"html", html},
                                                {"beforeText", change.baseline.text},
                                                {"afterText", change.current.text},
                                                {"kind", ReviewChangeClassifier::kindKey(change.kind)},
                                                {"noteText", change.noteText}});
                    }
                    return rows;
                }
                catch (...) {
                    return std::unexpected(failure);
                }
            });
        m_busy = true;
        m_watcher.setFuture(future);
        emit busyChanged();
    }
    catch (...) {
        m_busy = false;
        emit busyChanged();
        setError(tr("Unable to calculate proofreading changes."));
    }
}
