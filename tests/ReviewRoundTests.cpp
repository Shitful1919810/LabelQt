#include "core/LabelPlusDocument.h"
#include "core/ProjectMetadataService.h"
#include "services/automation/ProjectTaskService.h"
#include "services/label/LabelEditController.h"
#include "services/project/ProjectLocationService.h"
#include "services/project/ProjectRecoveryService.h"
#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ProofreadReportService.h"
#include "services/proofread/ReviewRoundEditService.h"
#include "ui/labeltable/LabelTableModel.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include "ui/proofread/ProofreadNotesWidget.h"
#include "ui/proofread/ProofreadingController.h"
#include <QAbstractItemModelTester>
#include <QCheckBox>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <thread>

using namespace labelqt::core;
using namespace labelqt::services;
namespace {
struct Host {
    QTemporaryDir directory;
    ProjectController controller;
    UndoStack undo;
    LabelEditController editor{controller, undo, {}};
    ProjectTaskService tasks{controller, editor};
    Host()
    {
        Project project;
        project.setFilePath(directory.filePath("project.txt"));
        project.setGroups({"dialogue"});
        project.images() = {{"a.png",
                             directory.filePath("a.png"),
                             {Label("hello", "dialogue", {0.23, 0.45678}), Label("second", "dialogue", {0.4, 0.6})}},
                            {"b.png", directory.filePath("b.png"), {Label("third", "dialogue", {0.5, 0.5})}}};
        for (const auto& page : std::as_const(project).images()) {
            QImage image(300, 420, QImage::Format_RGB32);
            image.fill(Qt::white);
            if (!image.save(page.path))
                qFatal("Cannot create test image");
        }
        controller.setProject(project);
    }
    Project& project()
    {
        return controller.project();
    }
    ReviewRound round()
    {
        ReviewRound result;
        result.before = ReviewRoundService::snapshot(project());
        return result;
    }
    ProofreadNote note()
    {
        ProofreadNote note;
        const auto& page = std::as_const(project()).images().first();
        note.pageId = page.id;
        note.labelId = page.labels.first().id();
        note.pageName = page.name;
        note.quote = page.labels.first().text();
        note.text = "Reason <script>alert(1)</script>";
        return note;
    }
    bool commit(const std::optional<ReviewRound>& round)
    {
        return ReviewRoundEditService::commit(controller, undo, ReviewRoundEditService::token(controller), round,
                                              "review")
            .has_value();
    }
    ProjectTaskRequest request(bool metadata)
    {
        ProjectTaskRequest request;
        request.pages.insert(project().images()[0].id);
        request.writePages = request.pages;
        request.writeFields = {ProjectField::Text};
        request.readMetadata = metadata;
        return request;
    }
};
} // namespace

class ReviewRoundTests final : public QObject {
    Q_OBJECT
private slots:
    void recordRoundTripDoesNotChangeClassicBody()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        QVERIFY(host.commit(round));
        host.editor.setLabelText(0, 0, "reviewed");
        round.after = ReviewRoundService::snapshot(host.project());
        QVERIFY(host.commit(round));
        const auto serialized = LabelPlusDocument::serialize(host.project());
        const auto loaded = LabelPlusDocument::parse(serialized, host.project().filePath());
        const auto restored = ReviewRoundService::read(loaded);
        QVERIFY(restored && restored->has_value());
        QCOMPARE((**restored).before.images()[0].labels[0].text(), "hello");
        QCOMPARE((**restored).after->images()[0].labels[0].text(), "reviewed");
        QCOMPARE((**restored).before.images()[0].labels[0].position().y(), 0.45678);
        QCOMPARE((**restored).notes.first().text, round.notes.first().text);
        QCOMPARE(loaded.images()[0].labels[0].text(), "reviewed");
        QCOMPARE(loaded.images()[0].labels[0].id(), host.project().images()[0].labels[0].id());
        QCOMPARE(ReviewRoundService::encode(**restored, loaded.filePath()),
                 ReviewRoundService::encode(round, loaded.filePath()));
        QVERIFY((**restored).before.commentLines().isEmpty());
        const auto body = serialized.mid(serialized.indexOf(">>>>>>>>"));
        QVERIFY(!body.contains("Reason"));
        QVERIFY(!body.contains("hello"));
    }

    void identityMatchingSurvivesRewriteAndReorder()
    {
        Host host;
        const auto round = host.round();
        host.editor.setLabelText(0, 0, "completely unrelated wording");
        host.project().images()[0].labels.swapItemsAt(0, 1);
        host.project().images()[0].name = "renamed.png";
        host.project().images().swapItemsAt(0, 1);
        const auto result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.size(), 2);
        QVERIFY(!result.uncertainIdentity);
        for (const auto& change : result.changes) {
            QCOMPARE(change.kind, ReviewChangeKind::Modified);
            QCOMPARE(change.baseline.id, change.current.id);
            QVERIFY(change.orderChanged);
        }
    }

    void deleteAndReinsertIsNotAHeuristicMatch()
    {
        Host host;
        const auto round = host.round();
        auto& labels = host.project().images()[0].labels;
        const auto old = labels.takeFirst();
        labels.prepend(Label(old.text(), old.group(), old.position()));
        const auto result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.size(), 2);
        QVERIFY(
            std::ranges::any_of(result.changes, [](const auto& item) { return item.kind == ReviewChangeKind::Added; }));
        QVERIFY(std::ranges::any_of(result.changes,
                                    [](const auto& item) { return item.kind == ReviewChangeKind::Deleted; }));
        for (const auto& change : result.changes)
            QVERIFY(!change.orderChanged);
    }

    void completionFreezesDiffButImagePathsFollowPageIdentity()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        host.editor.setLabelText(0, 0, "reviewed");
        round.after = ReviewRoundService::snapshot(host.project());
        QVERIFY(host.commit(round));
        host.project().images()[0].labels.clear();
        host.project().images()[0].name = "cover.png";
        host.project().images()[0].path = host.directory.filePath("cover.png");
        host.project().images().swapItemsAt(0, 1);
        const auto result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.size(), 1);
        QCOMPARE(result.changes.first().baseline.text, "hello");
        QCOMPARE(result.changes.first().current.text, "reviewed");
        QCOMPARE(result.after.images()[0].path, host.directory.filePath("cover.png"));
        QCOMPARE(round.after->images()[0].name, "a.png");
        const auto destination =
            ReviewRoundService::locate(host.project(), round.notes[0].pageId, round.notes[0].labelId);
        QCOMPARE(destination, qMakePair(1, -1));
        host.project().images().clear();
        QCOMPARE(ReviewRoundService::present(round, host.project()).changes.size(), 1);
    }

    void commentsWithoutEditsAppearInModelAndReport()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        auto result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.size(), 1);
        QCOMPARE(result.changes.first().kind, ReviewChangeKind::Comment);
        ProofreadChangeTableModel model(result.changes, {});
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ProofreadChangeFilterModel filter;
        filter.setSourceModel(&model);
        filter.setFilters({"a.png"}, {"comment"}, {"comment"}, false);
        QCOMPARE(filter.rowCount(), 1);
        filter.setFilters({"b.png"}, {"comment"}, {"comment"}, false);
        QCOMPARE(filter.rowCount(), 0);
        const auto html = ProofreadReportService::htmlReport(result.changes, result.before, result.after, {});
        QVERIFY(html.contains("hello"));
        QVERIFY(html.contains("Reason &lt;script&gt;"));
        QVERIFY(!html.contains("<script>alert(1)</script>"));
        result.after.setCommentLines(ReviewSessionService::withProjectSession({}, {}));
        ProofreadChangesDialog dialog(result.before, result.after, {}, {}, result.changes,
                                      {.leftProjectTitle = {},
                                       .rightProjectTitle = {},
                                       .reviewEnabled = true,
                                       .projectReviewSession = true,
                                       .roundReview = true});
        dialog.show();
        QCoreApplication::processEvents();
        QCOMPARE(dialog.findChild<QTableView*>()->model()->rowCount(), 1);
        const auto output = qEnvironmentVariable("LABELQT_TEST_SCREENSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(dialog.grab().save(QDir(output).filePath("proofreading-round.png")));
        }
    }

    void externalEditsInvalidateIdentityWithoutLosingComments()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        QVERIFY(host.commit(round));
        auto serialized = LabelPlusDocument::serialize(host.project());
        serialized.replace("hello", "external edit");
        const auto loaded = LabelPlusDocument::parse(serialized, host.project().filePath());
        QCOMPARE(loaded.images()[0].id, host.project().images()[0].id);
        QVERIFY(loaded.images()[0].labelIdentityGeneration != host.project().images()[0].labelIdentityGeneration);
        QVERIFY(loaded.images()[0].labels[0].id() != round.notes[0].labelId);
        const auto restored = ReviewRoundService::read(loaded);
        QVERIFY(restored && restored->has_value());
        QCOMPARE((**restored).notes.size(), 1);
        const auto result = ReviewRoundService::present(**restored, loaded);
        QVERIFY(result.uncertainIdentity);
        QVERIFY(std::ranges::any_of(result.changes,
                                    [](const auto& item) { return item.noteText.contains("association"); }));
        QCOMPARE(ReviewRoundService::locate(loaded, round.notes[0].pageId, round.notes[0].labelId).second, -1);
    }

    void metadataEditsRespectAutomationDependenciesAndRejectStaleDialogs()
    {
        Host host;
        const auto independent = host.tasks.capture(host.request(false));
        const auto dependent = host.tasks.capture(host.request(true));
        QVERIFY(independent && dependent);
        auto round = host.round();
        QVERIFY(host.commit(round));
        const auto page = host.project().images()[0];
        LabelChange edit{page.id, page.labels[0].id(), QStringLiteral("automated"), {}, {}, {}};
        QVERIFY(!host.tasks.submit(dependent->taskId, {edit}, "dependent"));
        const auto token = ReviewRoundEditService::token(host.controller);
        QVERIFY(host.tasks.submit(independent->taskId, {edit}, "independent"));
        QVERIFY(!ReviewRoundEditService::commit(host.controller, host.undo, token, std::nullopt, "stale"));
        QCOMPARE(ReviewRoundService::present(round, host.project()).changes.first().current.text, "automated");
        const auto fresh = ReviewRoundEditService::token(host.controller);
        bool accepted = true;
        std::thread worker([&] {
            accepted =
                ReviewRoundEditService::commit(host.controller, host.undo, fresh, std::nullopt, "worker").has_value();
        });
        worker.join();
        QVERIFY(!accepted);
    }

    void undoPreservesUnrelatedMetadataAndSurvivesSaveAs()
    {
        Host host;
        auto round = host.round();
        QVERIFY(host.commit(round));
        QTemporaryDir elsewhere;
        host.controller.saveAs(elsewhere.filePath("copy.txt"));
        auto metadata = ProjectMetadataService::metadataObject(host.project().commentLines());
        metadata.insert("unrelated", "keep");
        host.project().setCommentLines(
            ProjectMetadataService::rewriteCommentLines(host.project().commentLines(), metadata));
        host.undo.undo();
        QVERIFY(!ReviewRoundService::read(host.project())->has_value());
        QCOMPARE(ProjectMetadataService::metadataObject(host.project().commentLines()).value("unrelated").toString(),
                 "keep");
        host.undo.redo();
        const auto restored = ReviewRoundService::read(host.project());
        QVERIFY(restored && restored->has_value());
        QCOMPARE((**restored).before.images()[0].path, round.before.images()[0].path);
        QVERIFY(host.commit(std::nullopt));
        QCOMPARE(host.project().images()[0].labels[0].text(), "hello");
        host.undo.undo();
        QVERIFY(ReviewRoundService::read(host.project())->has_value());
    }

    void invalidRecordsArePreservedAndDoNotBecomeLiveLabels()
    {
        Host host;
        QVERIFY(host.commit(host.round()));
        auto metadata = ProjectMetadataService::metadataObject(host.project().commentLines());
        auto section = metadata.value("proofreading").toObject();
        section.insert("version", 900);
        metadata.insert("proofreading", section);
        host.project().setCommentLines(
            ProjectMetadataService::rewriteCommentLines(host.project().commentLines(), metadata));
        const auto original = host.project().commentLines();
        QVERIFY(!ReviewRoundService::read(host.project()));
        const auto loaded =
            LabelPlusDocument::parse(LabelPlusDocument::serialize(host.project()), host.project().filePath());
        QVERIFY(!ReviewRoundService::read(loaded));
        QCOMPARE(host.project().commentLines(), original);
        QCOMPARE(loaded.images()[0].labels.size(), 2);
    }

    void legacyBaselineCanBeCompletedWithoutLosingOriginalText()
    {
        Host host;
        const auto metadata = ReviewMetadataService::captureBaseline(host.project());
        host.project().setCommentLines(ReviewMetadataService::rewriteCommentLines({}, metadata));
        host.editor.setLabelText(0, 0, "reviewed");
        const auto legacy = ReviewRoundService::read(host.project());
        QVERIFY(legacy && legacy->has_value());
        auto round = **legacy;
        QCOMPARE(round.before.images()[0].labels[0].text(), "hello");
        round.after = ReviewRoundService::snapshot(host.project());
        QVERIFY(host.commit(round));
        QVERIFY(!ProjectMetadataService::metadataObject(host.project().commentLines()).contains("review"));
        QCOMPARE(ReviewRoundService::present(round, host.project()).changes.first().baseline.text, "hello");
    }

    void notesWidgetDoesNotRetainLiveProjectContainers()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        ProofreadNotesWidget widget;
        widget.refresh(round, host.project(), 0, 0, true);
        const auto* labels = &std::as_const(host.project()).images()[0].labels;
        host.project().images()[0].labels[0].setText("edit");
        QCOMPARE(&std::as_const(host.project()).images()[0].labels, labels);
        QCOMPARE(widget.findChild<QListWidget*>()->count(), 1);
        widget.refresh(round, host.project(), 1, 0, true);
        QCOMPARE(widget.findChild<QListWidget*>()->count(), 0);
        widget.findChild<QCheckBox*>()->setChecked(true);
        QCOMPARE(widget.findChild<QListWidget*>()->count(), 1);
    }

    void completedRecordRejectsRewritingButAllowsAcknowledgementAndRelinking()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        QVERIFY(host.commit(round));
        round = ReviewRoundService::complete(round, host.project());
        QVERIFY(host.commit(round));
        auto changed = round;
        changed.notes[0].text = "replace author's reason";
        QVERIFY(!host.commit(changed));
        changed = round;
        changed.after->images()[0].labels[0].setText("rewrite completed draft");
        QVERIFY(!host.commit(changed));
        const auto original = ReviewRoundService::present(round, host.project());
        round.notes[0].resolved = true;
        round.notes[0].associatedPageId = host.project().images()[1].id;
        round.notes[0].associatedLabelId = host.project().images()[1].labels[0].id();
        QVERIFY(host.commit(round));
        const auto result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.first().current.id, original.changes.first().current.id);
        QCOMPARE(ReviewRoundService::locateNote(host.project(), round.notes[0]), qMakePair(1, 0));
    }

    void noteEditsInvalidateReadStatusButResolvingDoesNot()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        const auto fingerprint = [&](const ReviewRound& value) {
            return ReviewSessionService::fingerprint(
                ReviewRoundService::present(value, host.project()).changes.first());
        };
        const auto before = fingerprint(round);
        round.notes[0].resolved = true;
        QCOMPARE(fingerprint(round), before);
        round.notes[0].text += " more information";
        QVERIFY(fingerprint(round) != before);
    }

    void fileRenameTransactionPreservesCompletedRecord()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        host.editor.setLabelText(0, 0, "reviewed");
        round = ReviewRoundService::complete(round, host.project());
        QVERIFY(host.commit(round));
        host.controller.save();
        const auto original =
            ProjectMetadataService::metadataObject(host.project().commentLines()).value("proofreading");
        const auto plan = ProjectImageRenameService::createPlan(host.project(), {"cover", 1, {0}});
        QVERIFY(plan.isValid());
        QVERIFY(host.controller.renameImages(plan));
        const auto loaded = LabelPlusDocument::loadFromFile(host.project().filePath());
        QCOMPARE(ProjectMetadataService::metadataObject(loaded.commentLines()).value("proofreading"), original);
        const auto restored = ReviewRoundService::read(loaded);
        QVERIFY(restored && restored->has_value());
        const auto result = ReviewRoundService::present(**restored, loaded);
        QCOMPARE(result.after.images()[0].path, host.directory.filePath("cover.png"));
        QCOMPARE(result.changes.first().current.text, "reviewed");
    }

    void reloadingPreventsOldDialogsAndUndoFromWritingIntoAnotherDocument()
    {
        Host host;
        auto round = host.round();
        QVERIFY(host.commit(round));
        host.controller.save();
        const auto token = ReviewRoundEditService::token(host.controller);
        const auto oldDocument = host.controller.documentSession();
        host.controller.loadFromFile(host.project().filePath());
        QVERIFY(host.controller.documentSession() != oldDocument);
        const auto comments = host.project().commentLines();
        QVERIFY(!ReviewRoundEditService::commit(host.controller, host.undo, token, std::nullopt, "stale"));
        host.undo.undo();
        QCOMPARE(host.project().commentLines(), comments);
    }

    void revisionInteractionsAreIndependentOfRoundCompletion()
    {
        Host host;
        QWidget parent;
        QMenu menu(&parent);
        QAction start(&parent), show(&parent), compare(&parent);
        QLabel status(&parent);
        ProofreadNotesWidget notes(&parent);
        LabelTableModel model;
        const AppPreferences preferences;
        ProofreadingController controller(&parent, host.controller, host.undo,
                                          {[&]() -> Project& { return host.project(); },
                                           [&]() -> const AppPreferences& { return preferences; }, [] {},
                                           [] { return false; }, [&] { host.controller.markDirty(); }, [](int, int) {},
                                           [](const QString&, int) {}, [] { return qMakePair(0, 0); }});
        controller.setUi(&start, &show, &compare, &status, &menu, &notes, &model);
        controller.toggleBaseline();
        QVERIFY(controller.isActive());
        const auto commentsBeforeComparison = host.project().commentLines();
        QTimer cancelComparison;
        connect(&cancelComparison, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        cancelComparison.start(1);
        controller.showChanges();
        cancelComparison.stop();
        QThreadPool::globalInstance()->waitForDone();
        QCOMPARE(host.project().commentLines(), commentsBeforeComparison);
        menu.actions().first()->setChecked(false);
        QVERIFY(!controller.isActive());
        QVERIFY(ReviewRoundService::read(host.project())->has_value());
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Ok)->click();
        });
        controller.toggleBaseline();
        QVERIFY((**ReviewRoundService::read(host.project())).after.has_value());
        QVERIFY(show.isEnabled());
        QVERIFY(!controller.isActive());
        host.undo.undo();
        controller.updateUi(false);
        QVERIFY(!(**ReviewRoundService::read(host.project())).after.has_value());
        host.project().images().clear();
        controller.updateUi(false);
        QVERIFY(start.isEnabled());
        QVERIFY(menu.actions().last()->isEnabled());
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Ok)->click();
        });
        controller.toggleBaseline();
        QVERIFY((**ReviewRoundService::read(host.project())).after.has_value());
    }

    void reusedPageNameDoesNotRestoreOldCommentTarget()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        host.project().images()[0] = {
            "a.png", host.directory.filePath("a.png"), {Label("replacement", "dialogue", {0.2, 0.2})}};
        const auto result = ReviewRoundService::present(round, host.project());
        for (const auto& change : result.changes)
            if (!change.noteText.isEmpty()) {
                QCOMPARE(change.pageId, round.notes[0].pageId);
                QVERIFY(!change.jumpAvailable);
            }
        QCOMPARE(ReviewRoundService::locateNote(host.project(), round.notes[0]), qMakePair(-1, -1));
    }

    void malformedLegacyRecordsCannotBecomeEmptyRounds()
    {
        Host host;
        host.project().setCommentLines(ProjectMetadataService::rewriteCommentLines(
            {}, {{"review", QJsonObject{{"version", 1}, {"mode", "baseline"}, {"labels", "invalid"}}}}));
        const auto original = host.project().commentLines();
        QVERIFY(!ReviewRoundService::read(host.project()));
        QVERIFY(!host.commit(host.round()));
        QCOMPARE(host.project().commentLines(), original);
    }

    void restoringBackupInvalidatesRoundUndoAndAutomation()
    {
        Host host;
        QVERIFY(host.commit(host.round()));
        host.controller.save();
        const AppPreferences preferences;
        const auto backup = ProjectRecoveryService::preserveLocalEdits(host.project(), preferences);
        QVERIFY(backup);
        const auto entries = ProjectRecoveryService::entries(host.project().filePath(), preferences);
        QVERIFY(!entries.isEmpty());
        const auto document = host.controller.documentSession();
        const auto token = ReviewRoundEditService::token(host.controller);
        const auto task = host.tasks.capture(host.request(false));
        QVERIFY(task);
        QVERIFY(host.controller.restoreBackup(entries.first(), preferences));
        QVERIFY(host.controller.documentSession() != document);
        const auto comments = host.project().commentLines();
        host.undo.undo();
        QCOMPARE(host.project().commentLines(), comments);
        QVERIFY(!ReviewRoundEditService::commit(host.controller, host.undo, token, std::nullopt, "stale"));
        const auto& page = std::as_const(host.project()).images()[0];
        QVERIFY(!host.tasks.submit(task->taskId, {LabelChange{page.id, page.labels[0].id(), "stale", {}, {}, {}}},
                                   "stale"));
    }

    void damagedNoteAnchorsAreRejectedInsteadOfBecomingPageComments()
    {
        Host host;
        auto round = host.round();
        round.notes.append(host.note());
        const auto original = ReviewRoundService::encode(round, host.project().filePath());
        for (const bool missingPage : {false, true}) {
            auto record = original;
            auto notes = record.value("notes").toArray();
            auto note = notes[0].toObject();
            if (missingPage)
                note.insert("associatedLabelId", QUuid::createUuid().toString());
            else
                note.insert("labelId", false);
            notes[0] = note;
            record.insert("notes", notes);
            host.project().setCommentLines(ProjectMetadataService::rewriteCommentLines({}, {{"proofreading", record}}));
            QVERIFY(!ReviewRoundService::read(host.project()));
            QVERIFY(!host.commit(round));
        }
    }

    void relinkedPageCommentTracksItsActualLabelAndCurrentPageName()
    {
        Host host;
        auto round = host.round();
        auto note = host.note();
        note.labelId = {};
        note.associatedPageId = host.project().images()[1].id;
        note.associatedLabelId = host.project().images()[1].labels[0].id();
        round.notes.append(note);
        host.project().images()[1].name = "renamed.png";
        ProofreadNotesWidget widget;
        widget.refresh(round, host.project(), 1, 0, true);
        auto* list = widget.findChild<QListWidget*>();
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains("renamed.png"));
        host.project().images()[1].labels.clear();
        widget.refresh(round, host.project(), 1, -1, true);
        widget.findChild<QCheckBox*>()->setChecked(true);
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains("Needs association"));
    }

    void pageCommentsDoNotInventMarkersAndLabelCommentsKeepBothIndexes()
    {
        Host host;
        auto round = host.round();
        auto note = host.note();
        note.labelId = {};
        round.notes.append(note);
        auto result = ReviewRoundService::present(round, host.project());
        auto html = ProofreadReportService::htmlReport(result.changes, result.before, result.after, {});
        QVERIFY(html.contains("Reason"));
        QVERIFY(!html.contains("<span class=\"marker "));
        round.notes[0] = host.note();
        result = ReviewRoundService::present(round, host.project());
        QCOMPARE(result.changes.first().baselineLabelIndex, 0);
        QCOMPARE(result.changes.first().currentLabelIndex, 0);
        html = ProofreadReportService::htmlReport(result.changes, result.before, result.after, {});
        QCOMPARE(html.count("<span class=\"marker "), 2);
    }

    void reportDoesNotCombineDeletedAndReplacementPagesWithTheSameName()
    {
        Host host;
        const auto round = host.round();
        host.project().images()[0] = {
            "a.png", host.directory.filePath("b.png"), {Label("replacement", "dialogue", {0.5, 0.5})}};
        const auto result = ReviewRoundService::present(round, host.project());
        const auto html = ProofreadReportService::htmlReport(result.changes, result.before, result.after, {});
        QCOMPARE(html.count("<section class=\"page-section\""), 2);
    }

    void newLabelWithIdenticalContentDoesNotInheritReviewState()
    {
        Host host;
        const auto round = host.round();
        host.project().images()[0].labels.append(Label("new", "dialogue", {0.5, 0.5}));
        const auto first = ReviewRoundService::present(round, host.project()).changes.first();
        host.project().images()[0].labels.last() = Label("new", "dialogue", {0.5, 0.5});
        const auto second = ReviewRoundService::present(round, host.project()).changes.first();
        QVERIFY(ReviewSessionService::fingerprint(first) != ReviewSessionService::fingerprint(second));
    }

    void externalComparisonKeepsIdentityMatchingWhenOpeningTheReviewUi()
    {
        Host host;
        const auto before = ReviewRoundService::snapshot(host.project());
        host.project().images()[0].labels[0] = Label("hello", "dialogue", {0.23, 0.45678});
        const auto after = ReviewRoundService::snapshot(host.project());
        const auto plan = ProjectComparisonService::planComparison(before, after);
        const auto changes = ProjectComparisonService::changesBetweenProjects(after, plan);
        QCOMPARE(changes.size(), 2);
        ProofreadChangesDialog dialog(before, after, {}, plan, changes,
                                      {.leftProjectTitle = {}, .rightProjectTitle = {}, .reviewEnabled = true});
        QCOMPARE(dialog.findChild<QTableView*>()->model()->rowCount(), 2);
        const auto unchanged = plan.baselineMetadata.baselineFor("a.png", 1);
        const ManualLabelPair manual{unchanged, unchanged};
        const auto paired =
            ProjectComparisonService::changesWithPairs(after, plan.baselineMetadata, plan.pagePairs, {manual});
        QVERIFY(paired);
        QCOMPARE(paired->size(), 3); // Explicit pair plus the independent deletion and insertion.
    }
};
QTEST_MAIN(ReviewRoundTests)
#include "ReviewRoundTests.moc"
