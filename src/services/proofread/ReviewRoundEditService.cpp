#include "services/proofread/ReviewRoundEditService.h"
#include "core/ProjectMetadataService.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <stdexcept>

namespace labelqt::services {
namespace {
QJsonObject ownedSections(const QJsonObject& root)
{
    QJsonObject result;
    for (const auto key : {"review", "reviewSession", "proofreading"})
        if (root.contains(key))
            result.insert(key, root.value(key));
    return result;
}
QStringList applySections(const QStringList& comments, const QJsonObject& sections)
{
    auto root = labelqt::core::ProjectMetadataService::tryMetadataObject(comments);
    if (!root)
        throw std::runtime_error(root.error().toStdString());
    for (const auto key : {"review", "reviewSession", "proofreading"}) {
        root->remove(key);
        if (sections.contains(key))
            root->insert(key, sections.value(key));
    }
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(comments, *root);
}

QJsonArray authoredNotes(QJsonArray notes)
{
    for (qsizetype i = 0; i < notes.size(); ++i) {
        auto note = notes.at(i).toObject();
        for (const auto key : {"resolved", "associatedPageId", "associatedLabelId"})
            note.remove(key);
        notes[i] = note;
    }
    return notes;
}
} // namespace

ReviewEditToken ReviewRoundEditService::token(ProjectController& controller)
{
    const auto& revisions = controller.revisions();
    return {revisions.session(), revisions.revision()};
}

std::expected<void, QString> ReviewRoundEditService::editNote(ReviewRound& round, const labelqt::core::Project& project,
                                                              const ReviewNoteEdit& edit)
{
    using Operation = ReviewNoteEdit::Operation;
    const auto error = [](const char* message) {
        return std::unexpected(QCoreApplication::translate("ReviewRoundEditService", message));
    };
    if (round.after && edit.operation != Operation::Resolve && edit.operation != Operation::Relink)
        return error(QT_TRANSLATE_NOOP("ReviewRoundEditService",
                                       "Completed comments are read-only. Start a new round to add or edit comments."));
    if ((edit.operation == Operation::Add || edit.operation == Operation::Edit) && edit.text.trimmed().isEmpty())
        return error(QT_TRANSLATE_NOOP("ReviewRoundEditService", "The note text must not be empty."));
    auto note =
        std::find_if(round.notes.begin(), round.notes.end(), [&](const auto& item) { return item.id == edit.noteId; });
    if (edit.operation != Operation::Add && note == round.notes.end())
        return error(QT_TRANSLATE_NOOP("ReviewRoundEditService", "The proofreading note no longer exists."));
    if (edit.operation == Operation::Add || edit.operation == Operation::Relink) {
        const auto target = ReviewRoundService::locate(project, edit.pageId, edit.labelId);
        if (edit.pageId.isNull() || target.first < 0 || (!edit.labelId.isNull() && target.second < 0) ||
            (edit.operation == Operation::Relink && edit.labelId.isNull()))
            return error(QT_TRANSLATE_NOOP("ReviewRoundEditService", "The note target no longer exists."));
        if (edit.operation == Operation::Relink) {
            note->associatedPageId = edit.pageId;
            note->associatedLabelId = edit.labelId;
        }
        else {
            ProofreadNote added;
            added.pageId = edit.pageId;
            added.labelId = edit.labelId;
            added.pageName = project.images().at(target.first).name;
            if (target.second >= 0)
                added.quote = project.images().at(target.first).labels.at(target.second).text();
            added.text = edit.text;
            round.notes.append(std::move(added));
        }
    }
    else if (edit.operation == Operation::Edit)
        note->text = edit.text;
    else if (edit.operation == Operation::Remove)
        round.notes.erase(note);
    else if (edit.operation == Operation::Resolve)
        note->resolved = edit.resolved;
    return {};
}

std::expected<void, QString> ReviewRoundEditService::commit(ProjectController& controller,
                                                            labelqt::core::UndoStack& undo, ReviewEditToken expected,
                                                            const std::optional<ReviewRound>& round,
                                                            const QString& description)
{
    if (!controller.isOwnerThread())
        return std::unexpected(QCoreApplication::translate("ReviewRoundEditService",
                                                           "Proofreading edits must run on the project thread."));
    const auto actual = token(controller);
    if (actual.session != expected.session || actual.revision != expected.revision)
        return std::unexpected(QCoreApplication::translate(
            "ReviewRoundEditService", "The project changed while this dialog was open. Please try again."));
    try {
        auto& project = controller.project();
        const auto existing = ReviewRoundService::read(project);
        if (!existing)
            return std::unexpected(existing.error());
        const auto root = labelqt::core::ProjectMetadataService::tryMetadataObject(project.commentLines());
        if (!root)
            return std::unexpected(root.error());
        const auto before = ownedSections(*root);
        QJsonObject after;
        if (round)
            after.insert("proofreading", ReviewRoundService::encode(*round, project.filePath()));
        if (before.contains("proofreading") && *existing && round && (**existing).id == round->id) {
            const auto oldRecord = ReviewRoundService::encode(**existing, project.filePath());
            const auto newRecord = after.value("proofreading").toObject();
            if (oldRecord.value("before") != newRecord.value("before") ||
                ((**existing).after && (oldRecord.value("after") != newRecord.value("after") ||
                                        authoredNotes(oldRecord.value("notes").toArray()) !=
                                            authoredNotes(newRecord.value("notes").toArray()))))
                return std::unexpected(QCoreApplication::translate(
                    "ReviewRoundEditService",
                    "Completed drafts and authored comments cannot be rewritten. Start a new round instead."));
        }
        const auto candidateComments = applySections(project.commentLines(), after);
        auto candidate = project;
        candidate.setCommentLines(candidateComments);
        const auto checked = ReviewRoundService::read(candidate);
        if (!checked)
            return std::unexpected(checked.error());
        if (before == after)
            return {};
        const QString path = project.filePath();
        const auto apply = [&controller, path, session = controller.documentSession()](const QJsonObject& sections) {
            if (!controller.isOwnerThread() || controller.documentSession() != session)
                return;
            auto adjusted = sections;
            if (adjusted.contains("proofreading"))
                adjusted.insert("proofreading", ReviewRoundService::rebase(adjusted.value("proofreading").toObject(),
                                                                           path, controller.project().filePath()));
            controller.project().setCommentLines(applySections(controller.project().commentLines(), adjusted));
            controller.markDirty();
        };
        apply(after);
        undo.push(description, [apply, before] { apply(before); }, [apply, after] { apply(after); });
        return {};
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}
} // namespace labelqt::services
