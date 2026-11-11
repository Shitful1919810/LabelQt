#include "core/LabelPlusDocument.h"
#include "core/ProjectMetadataService.h"
#include "mobile/MobileDocument.h"
#include "mobile/MobileReviewController.h"
#include "services/proofread/ReviewChangeClassifier.h"
#include "services/proofread/TextDiffHtmlRenderer.h"

#include <QAbstractItemModelTester>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <memory>
#include <utility>

using namespace labelqt::core;
using namespace labelqt::services;
using namespace labelqt::mobile;

namespace {
QString key(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}

struct Host {
    QTemporaryDir directory;
    MobileDocument document;
    MobileReviewController review{document.controller(), document.undoStack()};

    Host()
    {
        QObject::connect(&document, &MobileDocument::edited, &review, &MobileReviewController::refresh);
        QObject::connect(&review, &MobileReviewController::edited, &document, &MobileDocument::refresh);
    }

    QString writeProject(const QString& name = QStringLiteral("project"), bool invalidReview = false)
    {
        // Only fixture construction mutates detached values; live edits use the shared command services.
        Project project;
        project.setFilePath(directory.filePath(name + ".txt"));
        project.setGroups({"dialogue", "sound"});
        project.images() = {{"a.png",
                             directory.filePath("a.png"),
                             {Label("first", "dialogue", {0.2, 0.3}), Label("second", "sound", {0.4, 0.5}),
                              Label("third", "dialogue", {0.6, 0.7})}},
                            {"b.png", directory.filePath("b.png"), {Label("next page", "dialogue", {0.5, 0.5})}}};
        if (invalidReview)
            project.setCommentLines(
                ProjectMetadataService::rewriteCommentLines({}, {{"proofreading", QJsonObject{{"version", -1}}}}));
        LabelPlusDocument::saveToFile(project, project.filePath());
        return project.filePath();
    }

    const Project& project()
    {
        return std::as_const(document.controller()).project();
    }
    const Label& label(int page = 0, int index = 0)
    {
        return project().images().at(page).labels.at(index);
    }
    MobileLabelModel* model()
    {
        return qobject_cast<MobileLabelModel*>(document.labels());
    }
};
} // namespace

class MobileDocumentTests final : public QObject {
    Q_OBJECT
private slots:
    void groupStylesSurviveFilteringAndSelectionDoesNotResetModel()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        auto* model = host.model();
        QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
        const auto soundColor = model->index(1).data(MobileLabelModel::MarkerColor);
        QCOMPARE(soundColor.value<QColor>(), defaultLabelGroupStyles().at(1).groupColor);
        QVERIFY(model->index(1).data(MobileLabelModel::MarkerSquare).toBool());
        host.document.selectLabel(key(host.label().id()));
        host.document.updateText("incremental update");
        QVERIFY(host.document.commitText());
        QCOMPARE(resets.count(), 0);
        QCOMPARE(changes.count(), 1);
        host.document.refresh();
        QCOMPARE(changes.count(), 1);
        host.document.setVisibleGroups({"sound"});
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0).data(MobileLabelModel::MarkerColor), soundColor);
        host.document.setGroupStyles(
            {{QColor("#abcdef"), 20, 10, MarkerShape::Square}, {QColor("#123456"), 20, 10, MarkerShape::Circle}});
        QCOMPARE(model->index(0).data(MobileLabelModel::MarkerColor).value<QColor>(), QColor("#123456"));
        QVERIFY(!model->index(0).data(MobileLabelModel::MarkerSquare).toBool());
    }

    void undoneGroupCannotBecomeNewLabelGroup()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        host.document.setVisibleGroups({});
        host.document.addGroup("temporary");
        QCOMPARE(host.document.visibleGroups(), QStringList{"temporary"});
        host.document.undo();
        QVERIFY(host.document.visibleGroups().isEmpty());
        const auto snapshot = LabelPlusDocument::serialize(host.project());
        host.document.addLabel(0.5, 0.5);
        QCOMPARE(LabelPlusDocument::serialize(host.project()), snapshot);
        QVERIFY(host.document.canRedo());
        host.document.redo();
        QCOMPARE(host.document.visibleGroups(), QStringList{"temporary"});
        host.document.addLabel(0.5, 0.5);
        QCOMPARE(host.document.selectedGroup(), "temporary");
    }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void emptyDocumentAndFailedOpen()
    {
        Host host;
        QVERIFY(host.directory.isValid());
        QVERIFY(host.model());
        QAbstractItemModelTester tester(host.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(!host.document.opened());
        QCOMPARE(host.document.pageIndex(), -1);
        QVERIFY(host.document.imageSource().isEmpty());
        QCOMPARE(host.model()->rowCount(), 0);
        QVERIFY(host.document.save());
        host.document.updateText("no selected label");
        host.document.addLabel(0.2, 0.3);
        QVERIFY(!host.document.canUndo());
        QVERIFY(!host.document.openProject(host.directory.filePath("missing.txt")));
        QVERIFY(!host.document.error().isEmpty());
        QVERIFY(!host.document.opened());
        host.document.clearError();
        QVERIFY(host.document.error().isEmpty());
    }

    void draftSaveUndoAndReopenPreserveIdentity()
    {
        Host host;
        const auto path = host.writeProject();
        QVERIFY(host.document.openProject(path));
        QAbstractItemModelTester tester(host.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(host.document.title(), "project");
        QCOMPARE(host.document.pageNames(), QStringList({"a.png", "b.png"}));
        QCOMPARE(host.document.imageSource(), QUrl::fromLocalFile(host.directory.filePath("a.png")));
        const auto id = host.label().id();
        const auto pageId = host.project().images().first().id;
        host.document.selectLabel(key(id));
        const auto revision = host.document.controller().revisions().revision();
        QSignalSpy edits(&host.document, &MobileDocument::edited);
        host.document.updateText("saved <text>\nsecond line");
        QCOMPARE(host.label().text(), "first");
        QCOMPARE(host.model()->index(0).data(MobileLabelModel::Text).toString(), "first");
        QCOMPARE(host.document.controller().revisions().revision(), revision);
        QCOMPARE(edits.count(), 0);
        QVERIFY(host.document.dirty());
        QVERIFY(!host.document.canUndo());

        QVERIFY(host.document.save());
        QVERIFY(host.document.canUndo());
        QVERIFY(!host.document.dirty());
        QCOMPARE(edits.count(), 1);
        const auto saved = LabelPlusDocument::loadFromFile(path);
        QCOMPARE(saved.images().first().id, pageId);
        QCOMPARE(saved.images().first().labels.first().id(), id);
        QCOMPARE(saved.images().first().labels.first().text(), "saved <text>\nsecond line");
        QVERIFY(host.document.controller().revisions().revision() > revision);
        host.document.undo();
        QCOMPARE(host.label().text(), "first");
        QCOMPARE(host.document.selectedLabel(), key(id));
        QCOMPARE(host.document.textDraft(), "first");
        QVERIFY(host.document.canRedo());
        host.document.redo();
        QCOMPARE(host.label().text(), "saved <text>\nsecond line");
        const auto session = host.document.controller().documentSession();
        QVERIFY(host.document.openProject(path));
        QVERIFY(host.document.controller().documentSession() != session);
        QCOMPARE(host.label().id(), id);
        QVERIFY(host.document.selectedLabel().isEmpty());
        QVERIFY(!host.document.canUndo());
        QVERIFY(!host.document.canRedo());
    }

    void modelFilteringIsReadOnlyAndKeepsSourceNumbers()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        auto* model = host.model();
        QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QHash<int, QByteArray> roles{{MobileLabelModel::Key, "labelKey"},
                                           {MobileLabelModel::Text, "labelText"},
                                           {MobileLabelModel::Group, "groupName"},
                                           {MobileLabelModel::Number, "markerNumber"},
                                           {MobileLabelModel::PositionX, "positionX"},
                                           {MobileLabelModel::PositionY, "positionY"},
                                           {MobileLabelModel::MarkerColor, "markerColor"},
                                           {MobileLabelModel::MarkerSquare, "markerSquare"}};
        QCOMPARE(model->roleNames(), roles);
        const auto before = LabelPlusDocument::serialize(host.project());
        const auto revision = host.document.controller().revisions().revision();
        const auto hidden = key(host.label(0, 1).id());
        host.document.selectLabel(hidden);
        host.document.setVisibleGroups({"dialogue", "not a group", "dialogue"});
        QCOMPARE(host.document.visibleGroups(), QStringList({"dialogue"}));
        QVERIFY(host.document.selectedLabel().isEmpty());
        QVERIFY(host.document.textDraft().isEmpty());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(0).data(MobileLabelModel::Number).toInt(), 1);
        QCOMPARE(model->index(1).data(MobileLabelModel::Number).toInt(), 3);
        QCOMPARE(model->index(1).data(MobileLabelModel::Key).toString(), key(host.label(0, 2).id()));
        QCOMPARE(model->index(1).data(MobileLabelModel::PositionX).toDouble(), 0.6);
        QVERIFY(!model->setData(model->index(0), "bypass", MobileLabelModel::Text));
        QVERIFY(!model->insertRows(0, 1));
        QVERIFY(!model->removeRows(0, 1));
        QVERIFY(!(model->flags(model->index(0)) & Qt::ItemIsEditable));
        QVERIFY(!model->data({}, MobileLabelModel::Text).isValid());
        QCOMPARE(model->rowCount(model->index(0)), 0);
        host.document.selectLabel(hidden);
        QVERIFY(host.document.selectedLabel().isEmpty());
        host.document.setVisibleGroups({});
        QCOMPARE(model->rowCount(), 0);
        host.document.addLabel(0.5, 0.5);
        QCOMPARE(LabelPlusDocument::serialize(host.project()), before);
        QCOMPARE(host.document.controller().revisions().revision(), revision);
        QVERIFY(!host.document.canUndo());
        QVERIFY(!host.document.dirty());
    }

    void labelModelOwnsSnapshotAndExcludesDeletedRows()
    {
        MobileLabelModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVector<Label> source{Label("first", "dialogue", {0.2, 0.3}), Label("deleted", "dialogue", {}),
                              Label("last", "dialogue", {})};
        source[1].setDeleted(true);
        model.refresh(source, {"dialogue"}, {"dialogue"});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(1).data(MobileLabelModel::Number).toInt(), 3);
        source[0].setText("changed detached source");
        QCOMPARE(model.index(0).data(MobileLabelModel::Text).toString(), "first");
        model.refresh(source, {"dialogue"}, {"dialogue"});
        QCOMPARE(model.index(0).data(MobileLabelModel::Text).toString(), "changed detached source");
        model.refresh({}, {}, {});
        QCOMPARE(model.rowCount(), 0);
    }

    void selectionAndMoveFollowIdentityAfterSharedReorder()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        QAbstractItemModelTester tester(host.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto selected = key(host.label(0, 1).id());
        const auto other = key(host.label().id());
        host.document.selectLabel(selected);
        LabelEditController editor(host.document.controller(), host.document.undoStack(), {});
        QVERIFY(editor.reorderLabels(0, {1}, 0).changed);
        host.document.refresh();
        QCOMPARE(host.document.selectedLabel(), selected);
        QCOMPARE(host.model()->index(0).data(MobileLabelModel::Key).toString(), selected);
        host.document.moveLabel(other, 0.9, 0.9);
        QCOMPARE(host.label().position(), QPointF(0.4, 0.5));
        host.document.moveLabel(selected, 0.8, 0.9);
        QCOMPARE(host.label().position(), QPointF(0.8, 0.9));
        QCOMPARE(host.label(0, 1).position(), QPointF(0.2, 0.3));
        host.document.undo();
        QCOMPARE(host.label().position(), QPointF(0.4, 0.5));
        host.document.undo();
        QCOMPARE(key(host.label(0, 1).id()), selected);
        QCOMPARE(host.document.selectedLabel(), selected);
        QCOMPARE(host.document.textDraft(), "second");
        host.document.redo();
        QCOMPARE(key(host.label().id()), selected);
    }

    void deletionAndAdditionUndoDoNotReuseIdentity()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        QAbstractItemModelTester tester(host.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto oldKey = key(host.label().id());
        host.document.selectLabel(oldKey);
        host.document.deleteSelected();
        QVERIFY(host.label().isDeleted());
        QCOMPARE(host.model()->rowCount(), 2);
        QVERIFY(host.document.selectedLabel().isEmpty());
        host.document.undo();
        QVERIFY(!host.label().isDeleted());
        QCOMPARE(key(host.label().id()), oldKey);
        QCOMPARE(host.model()->rowCount(), 3);
        host.document.redo();
        host.document.addLabel(0.7, 0.8);
        QCOMPARE(host.model()->rowCount(), 3);
        const auto addedKey = host.model()->index(2).data(MobileLabelModel::Key).toString();
        QVERIFY(!addedKey.isEmpty());
        QVERIFY(addedKey != oldKey);
        host.document.selectLabel(oldKey);
        QVERIFY(host.document.selectedLabel().isEmpty());
        const auto before = LabelPlusDocument::serialize(host.project());
        host.document.moveLabel(oldKey, 0.1, 0.1);
        QCOMPARE(LabelPlusDocument::serialize(host.project()), before);
        host.document.undo();
        QCOMPARE(host.model()->rowCount(), 2);
        host.document.redo();
        QCOMPARE(host.model()->rowCount(), 3);
        QCOMPARE(host.model()->index(2).data(MobileLabelModel::Key).toString(), addedKey);
    }

    void addedLabelIsSelectedForImmediateTextEntry()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        const auto count = host.model()->rowCount();
        host.document.addLabel(0.7, 0.8);
        QCOMPARE(host.model()->rowCount(), count + 1);
        const auto addedKey = host.model()->index(count).data(MobileLabelModel::Key).toString();
        QCOMPARE(host.document.selectedLabel(), addedKey);
        host.document.updateText("new label text");
        QVERIFY(host.document.commitText());
        QCOMPARE(host.project().images().first().labels.last().text(), "new label text");
        QCOMPARE(host.label().text(), "first");
    }

    void pageNavigationCommitsDraftAndRejectsPreviousPageKey()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        QAbstractItemModelTester tester(host.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto previousKey = key(host.label().id());
        host.document.selectLabel(previousKey);
        host.document.updateText("committed on navigation");
        host.document.setPage(1);
        QCOMPARE(host.document.pageIndex(), 1);
        QCOMPARE(host.label().text(), "committed on navigation");
        QCOMPARE(host.model()->rowCount(), 1);
        QVERIFY(host.document.selectedLabel().isEmpty());
        QVERIFY(host.document.textDraft().isEmpty());
        host.document.selectLabel(previousKey);
        QVERIFY(host.document.selectedLabel().isEmpty());
        host.document.moveLabel(previousKey, 0.9, 0.9);
        QCOMPARE(host.label(1).position(), QPointF(0.5, 0.5));
        host.document.setPage(0);
        host.document.selectLabel(previousKey);
        QCOMPARE(host.document.textDraft(), "committed on navigation");
        host.document.undo();
        QCOMPARE(host.label().text(), "first");
        QCOMPARE(host.document.selectedLabel(), previousKey);
    }

    void conflictingDraftBlocksSaveAndDocumentSwitch()
    {
        Host host;
        const auto path = host.writeProject();
        const auto other = host.writeProject("other");
        QVERIFY(host.document.openProject(path));
        host.document.selectLabel(key(host.label().id()));
        host.document.updateText("uncommitted mobile draft");
        LabelEditController editor(host.document.controller(), host.document.undoStack(), {});
        QVERIFY(editor.setLabelText(0, 0, "external shared edit").changed);
        host.document.refresh();
        QVERIFY(!host.document.commitText());
        QVERIFY(!host.document.error().isEmpty());
        QVERIFY(!host.document.save());
        host.document.setPage(1);
        QCOMPARE(host.document.pageIndex(), 0);
        QVERIFY(!host.document.openProject(other));
        QCOMPARE(host.project().filePath(), path);
        QCOMPARE(host.document.textDraft(), "uncommitted mobile draft");
        QCOMPARE(host.label().text(), "external shared edit");
        QCOMPARE(LabelPlusDocument::loadFromFile(path).images().first().labels.first().text(), "first");
        host.document.undoStack().undo();
        host.document.refresh();
        QVERIFY(host.document.commitText());
        QVERIFY(host.document.save());
        QVERIFY(host.document.openProject(other));
        QVERIFY(host.document.selectedLabel().isEmpty());
        QVERIFY(!host.document.canUndo());
        QCOMPARE(LabelPlusDocument::loadFromFile(path).images().first().labels.first().text(),
                 "uncommitted mobile draft");
    }

    void reviewNotesUseSharedUndoAndFreezeOnCompletion()
    {
        Host host;
        const auto path = host.writeProject();
        QVERIFY(host.document.openProject(path));
        const auto pageId = key(host.project().images().first().id);
        const auto labelId = key(host.label().id());
        QSignalSpy edits(&host.review, &MobileReviewController::edited);
        host.review.startRound();
        QCOMPARE(host.review.roundState(), "active");
        QCOMPARE(edits.count(), 1);
        const auto started = host.project().commentLines();
        host.review.startRound();
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), started);
        QCOMPARE(edits.count(), 1);
        host.review.startRound(true);
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), started);
        QCOMPARE(edits.count(), 1);
        host.document.undo();
        QCOMPARE(host.review.roundState(), "none");
        host.document.redo();
        QCOMPARE(host.review.roundState(), "active");

        QVERIFY(host.review.addNote(pageId, labelId, "Check <script> & wording"));
        QVERIFY(host.review.error().isEmpty());
        QCOMPARE(host.review.notes().size(), 1);
        const auto noteId = host.review.notes().first().toMap().value("key").toString();
        QVERIFY(!QUuid(noteId).isNull());
        QCOMPARE(host.review.notes().first().toMap().value("text").toString(), "Check <script> & wording");
        QCOMPARE(host.review.notes().first().toMap().value("pageName").toString(), "a.png");
        host.document.undo();
        QVERIFY(host.review.notes().isEmpty());
        host.document.redo();
        QCOMPARE(host.review.notes().first().toMap().value("key").toString(), noteId);
        QVERIFY(host.review.addNote(pageId, {}, "Whole page"));
        QCOMPARE(host.review.notes().size(), 2);
        host.review.completeRound();
        QCOMPARE(host.review.roundState(), "completed");
        host.document.undo();
        QCOMPARE(host.review.roundState(), "active");
        host.document.redo();
        QCOMPARE(host.review.roundState(), "completed");
        const auto completed = host.project().commentLines();
        QVERIFY(!host.review.addNote(pageId, labelId, "too late"));
        QVERIFY(!host.review.error().isEmpty());
        host.review.startRound();
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), completed);
        host.review.resolveNote(noteId, true);
        QVERIFY(host.review.notes().first().toMap().value("resolved").toBool());
        host.document.undo();
        QVERIFY(!host.review.notes().first().toMap().value("resolved").toBool());
        host.document.redo();
        QVERIFY(host.document.save());
        QVERIFY(host.document.openProject(path));
        QCOMPARE(host.review.roundState(), "completed");
        QCOMPARE(host.review.notes().size(), 2);
        QVERIFY(host.review.notes().first().toMap().value("resolved").toBool());
        const auto round = ReviewRoundService::read(host.project());
        QVERIFY(round && round->has_value());
        QCOMPARE((**round).notes.first().id, QUuid(noteId));
        QCOMPARE((**round).notes.first().labelId, QUuid(labelId));
        QCOMPARE((**round).notes.first().quote, "first");
        QVERIFY((**round).after.has_value());
    }

    void notesRejectInvalidTargetsAndResolveByStableIdentity()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        host.review.startRound();
        const auto pageId = key(host.project().images().first().id);
        const auto labelId = key(host.label().id());
        LabelEditController editor(host.document.controller(), host.document.undoStack(), {});
        QVERIFY(editor.reorderLabels(0, {0}, 3).changed);
        host.document.refresh();
        host.review.refresh();
        QVERIFY(host.review.addNote(pageId, labelId, "same identity after reorder"));
        const auto round = ReviewRoundService::read(host.project());
        QVERIFY(round && round->has_value());
        QCOMPARE((**round).notes.first().labelId, QUuid(labelId));
        QCOMPARE((**round).notes.first().quote, "first");
        const auto comments = host.project().commentLines();
        for (const auto& invalid : QStringList{"not-a-uuid", key(QUuid::createUuid()), key(host.label(1).id())}) {
            QVERIFY(!host.review.addNote(pageId, invalid, "must fail"));
            QVERIFY(!host.review.error().isEmpty());
            QCOMPARE(host.project().commentLines(), comments);
        }
        QVERIFY(!host.review.addNote(key(QUuid::createUuid()), {}, "missing page"));
        QVERIFY(!host.review.error().isEmpty());
        QVERIFY(!host.review.addNote(pageId, labelId, " \n "));
        QVERIFY(!host.review.error().isEmpty());
        host.review.resolveNote(key(QUuid::createUuid()), true);
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), comments);
        host.document.selectLabel(labelId);
        host.document.deleteSelected();
        const auto afterDelete = host.project().commentLines();
        QVERIFY(!host.review.addNote(pageId, labelId, "deleted target"));
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), afterDelete);
        QCOMPARE(host.review.notes().size(), 1);
    }

    void noteFailureReturnsFalseAndPreservesRedo()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        const auto pageId = key(host.project().images().first().id);
        const auto labelId = key(host.label().id());
        const auto initialComments = host.project().commentLines();
        QVERIFY(!host.review.addNote(pageId, labelId, "no active round"));
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.project().commentLines(), initialComments);
        QVERIFY(!host.document.canUndo());

        host.review.startRound();
        QVERIFY(host.review.addNote(pageId, labelId, "valid note"));
        QVERIFY(host.review.error().isEmpty());
        const auto originalNotes = host.review.notes();
        host.document.undo();
        QVERIFY(host.review.notes().isEmpty());
        QVERIFY(host.document.canRedo());
        const auto comments = host.project().commentLines();
        const auto revision = host.document.controller().revisions().revision();
        QSignalSpy edits(&host.review, &MobileReviewController::edited);
        // This passes adapter validation but exceeds the shared service's stored-note limit.
        QVERIFY(!host.review.addNote(pageId, labelId, QString(65537, QLatin1Char('x'))));
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(edits.count(), 0);
        QCOMPARE(host.project().commentLines(), comments);
        QCOMPARE(host.document.controller().revisions().revision(), revision);
        QVERIFY(host.review.notes().isEmpty());
        QVERIFY(host.document.canRedo());
        host.document.redo();
        QCOMPARE(host.review.notes(), originalNotes);
        QVERIFY(host.review.error().isEmpty());
    }

    void completedReplacementRequiresConsentAndUndoRestoresWholeRound()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        const auto pageId = key(host.project().images().first().id);
        const auto labelId = key(host.label().id());
        host.review.startRound();
        QVERIFY(host.review.addNote(pageId, labelId, "resolved label note"));
        host.review.resolveNote(host.review.notes().first().toMap().value("key").toString(), true);
        QVERIFY(host.review.addNote(pageId, {}, "open page note"));
        host.document.selectLabel(labelId);
        host.document.updateText("completed draft");
        QVERIFY(host.document.commitText());
        host.review.completeRound();
        QCOMPARE(host.review.roundState(), "completed");
        const auto oldRound = ReviewRoundService::read(host.project());
        QVERIFY(oldRound && oldRound->has_value());
        const auto path = host.project().filePath();
        const auto oldRecord = ReviewRoundService::encode(**oldRound, path);
        const auto oldNotes = host.review.notes();
        QCOMPARE(oldNotes.size(), 2);
        QVERIFY(oldNotes.first().toMap().value("resolved").toBool());
        QVERIFY(!oldNotes.last().toMap().value("resolved").toBool());

        host.document.updateText("live text for the new baseline");
        QVERIFY(host.document.commitText());
        const auto liveSnapshot = LabelPlusDocument::serialize(ReviewRoundService::snapshot(host.project()));
        const auto comments = host.project().commentLines();
        const auto revision = host.document.controller().revisions().revision();
        QSignalSpy edits(&host.review, &MobileReviewController::edited);
        host.review.startRound();
        QVERIFY(!host.review.error().isEmpty());
        host.review.startRound(false);
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(host.review.roundState(), "completed");
        QCOMPARE(host.review.notes(), oldNotes);
        QCOMPARE(host.project().commentLines(), comments);
        QCOMPARE(host.document.controller().revisions().revision(), revision);
        QCOMPARE(edits.count(), 0);

        host.review.startRound(true);
        QVERIFY(host.review.error().isEmpty());
        QCOMPARE(host.review.roundState(), "active");
        QCOMPARE(edits.count(), 1);
        QVERIFY(host.review.notes().isEmpty());
        const auto newRound = ReviewRoundService::read(host.project());
        QVERIFY(newRound && newRound->has_value());
        QVERIFY(!(**newRound).id.isNull());
        QVERIFY((**newRound).id != (**oldRound).id);
        QVERIFY(!(**newRound).after.has_value());
        QVERIFY((**newRound).notes.isEmpty());
        QCOMPARE(LabelPlusDocument::serialize((**newRound).before), liveSnapshot);
        const auto newRecord = ReviewRoundService::encode(**newRound, path);

        host.document.undo();
        QCOMPARE(host.review.roundState(), "completed");
        QCOMPARE(host.review.notes(), oldNotes);
        const auto restored = ReviewRoundService::read(host.project());
        QVERIFY(restored && restored->has_value());
        QCOMPARE(ReviewRoundService::encode(**restored, path), oldRecord);
        QCOMPARE(host.label().text(), "live text for the new baseline");
        host.document.redo();
        QCOMPARE(host.review.roundState(), "active");
        QVERIFY(host.review.notes().isEmpty());
        const auto replayed = ReviewRoundService::read(host.project());
        QVERIFY(replayed && replayed->has_value());
        QCOMPARE(ReviewRoundService::encode(**replayed, path), newRecord);
        QCOMPARE(host.label().text(), "live text for the new baseline");
    }

    void malformedReviewCannotBeReadOrReplaced()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject("invalid", true)));
        QCOMPARE(host.review.roundState(), "invalid");
        const auto comments = host.project().commentLines();
        QSignalSpy edits(&host.review, &MobileReviewController::edited);
        host.review.startRound();
        host.review.startRound(true);
        host.review.completeRound();
        QVERIFY(!host.review.addNote(key(host.project().images().first().id), key(host.label().id()), "no"));
        host.review.resolveNote(key(QUuid::createUuid()), true);
        host.review.calculateChanges();
        QVERIFY(!host.review.error().isEmpty());
        QVERIFY(!host.review.busy());
        QVERIFY(host.review.changes().isEmpty());
        QVERIFY(host.review.notes().isEmpty());
        QCOMPARE(host.project().commentLines(), comments);
        QCOMPARE(edits.count(), 0);
        QVERIFY(!host.document.canUndo());
    }

    void diffPresentationUsesSharedServicesAndInvalidatesOnEdit()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        host.review.startRound();
        host.document.selectLabel(key(host.label().id()));
        host.document.updateText("<script>alert(1)</script>\nreplacement");
        QVERIFY(host.document.commitText());
        host.review.addNote(key(host.project().images().first().id), key(host.label().id()), "plain <note>");
        host.review.calculateChanges();
        QTRY_VERIFY_WITH_TIMEOUT(!host.review.busy(), 5000);
        QVERIFY2(host.review.error().isEmpty(), qPrintable(host.review.error()));
        const auto round = ReviewRoundService::read(host.project());
        QVERIFY(round && round->has_value());
        const auto expected = ReviewRoundService::present(**round, host.project());
        QCOMPARE(host.review.changes().size(), expected.changes.size());
        QCOMPARE(expected.changes.size(), 1);
        const auto& change = expected.changes.first();
        const auto row = host.review.changes().first().toMap();
        QCOMPARE(row.size(), 7);
        QCOMPARE(row.value("pageName").toString(), change.pageName());
        QCOMPARE(row.value("number").toInt(), change.labelIndex + 1);
        QCOMPARE(row.value("beforeText").toString(), change.baseline.text);
        QCOMPARE(row.value("afterText").toString(), change.current.text);
        QCOMPARE(row.value("kind").toString(), ReviewChangeClassifier::kindKey(change.kind));
        QCOMPARE(row.value("noteText").toString(), change.noteText);
        QCOMPARE(row.value("html").toString(),
                 TextDiffHtmlRenderer::renderInlineDiff(change.baseline.text, change.current.text, {}));
        QVERIFY(!row.value("html").toString().contains("<script>"));
        for (const auto* name : {"changes", "notes"})
            QVERIFY(!host.review.metaObject()->property(host.review.metaObject()->indexOfProperty(name)).isWritable());
        auto detached = host.review.changes();
        detached.clear();
        QCOMPARE(host.review.changes().size(), 1);
        host.document.updateText("later edit");
        QVERIFY(host.document.commitText());
        QVERIFY(host.review.changes().isEmpty());
        host.document.undo();
        QVERIFY(host.review.changes().isEmpty());
        host.review.completeRound();
        host.review.calculateChanges();
        QTRY_VERIFY_WITH_TIMEOUT(!host.review.busy(), 5000);
        const auto frozen = host.review.changes();
        QVERIFY(!frozen.isEmpty());
        host.document.updateText("post-completion edit");
        QVERIFY(host.document.commitText());
        QVERIFY(host.review.changes().isEmpty());
        host.review.calculateChanges();
        QTRY_VERIFY_WITH_TIMEOUT(!host.review.busy(), 5000);
        QCOMPARE(host.review.changes(), frozen);
    }

    void staleDiffIsDiscarded_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::newRow("revision") << QStringLiteral("edit");
        QTest::newRow("revision-restored-content") << QStringLiteral("edit-undo");
        QTest::newRow("same-file-new-session") << QStringLiteral("reload");
        QTest::newRow("different-document") << QStringLiteral("replace");
    }

    void staleDiffIsDiscarded()
    {
        QFETCH(QString, mutation);
        Host host;
        const auto path = host.writeProject();
        const auto other = host.writeProject("other");
        QVERIFY(host.document.openProject(path));
        host.review.startRound();
        host.document.selectLabel(key(host.label().id()));
        host.document.updateText("first revision");
        QVERIFY(host.document.commitText());
        QVERIFY(host.document.save());
        const auto session = host.document.controller().documentSession();
        QSignalSpy busy(&host.review, &MobileReviewController::busyChanged);
        QSignalSpy edits(&host.review, &MobileReviewController::edited);
        host.review.calculateChanges();
        QVERIFY(host.review.busy());
        // No event processing until after mutation: even an already-finished worker has a stale callback.
        if (mutation.startsWith("edit")) {
            host.document.updateText("second revision");
            QVERIFY(host.document.commitText());
            if (mutation == "edit-undo")
                host.document.undo();
        }
        else {
            QVERIFY(host.document.openProject(mutation == "reload" ? path : other));
            QVERIFY(host.document.controller().documentSession() != session);
        }
        const auto beforeCompletion = LabelPlusDocument::serialize(host.project());
        QTRY_VERIFY_WITH_TIMEOUT(!host.review.busy(), 5000);
        QVERIFY(host.review.changes().isEmpty());
        QVERIFY(!host.review.error().isEmpty());
        QCOMPARE(busy.count(), 2);
        QCOMPARE(edits.count(), 0);
        QCOMPARE(LabelPlusDocument::serialize(host.project()), beforeCompletion);
        QCOMPARE(host.review.roundState(), mutation == "replace" ? "none" : "active");
        if (mutation != "replace") {
            host.review.calculateChanges();
            QTRY_VERIFY_WITH_TIMEOUT(!host.review.busy(), 5000);
            QVERIFY2(host.review.error().isEmpty(), qPrintable(host.review.error()));
            QCOMPARE(host.review.changes().size(), 1);
            QCOMPARE(host.review.changes().first().toMap().value("afterText").toString(), host.label().text());
        }
    }

    void destroyingReviewDuringDiffDoesNotTouchDocument()
    {
        Host host;
        QVERIFY(host.document.openProject(host.writeProject()));
        host.review.startRound();
        host.document.selectLabel(key(host.label().id()));
        host.document.updateText("changed");
        QVERIFY(host.document.commitText());
        const auto before = LabelPlusDocument::serialize(host.project());
        auto preview = std::make_unique<MobileReviewController>(host.document.controller(), host.document.undoStack());
        preview->calculateChanges();
        QVERIFY(preview->busy());
        preview.reset();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        QCOMPARE(LabelPlusDocument::serialize(host.project()), before);
    }
};

QTEST_GUILESS_MAIN(MobileDocumentTests)
#include "MobileDocumentTests.moc"
