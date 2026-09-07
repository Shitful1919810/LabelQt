#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/FileRenameTransaction.h"
#include "services/label/LabelEditController.h"
#include "services/project/PageManagementDraft.h"
#include "services/project/PageSourceInfoService.h"
#include "services/project/ProjectController.h"
#include "services/project/ProjectLocationService.h"
#include "services/project/ProjectPageOrderService.h"
#include "services/project/ProjectRecoveryService.h"
#include "services/project/ProjectWorkflowController.h"
#include "services/proofread/ProjectComparisonService.h"
#include "services/proofread/ProofreadReportService.h"
#include "ui/project/PageOrderDialog.h"
#include "ui/project/PageOrderListModel.h"
#include "ui/project/ProjectRecoveryDialog.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include "ui/proofread/ProofreadReviewPanel.h"
#include "ui/search/ProjectSearchPanel.h"
#include "ui/search/ProjectTextSearchModel.h"
#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace labelqt::core;
using namespace labelqt::services;
namespace {
QByteArray bytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
Project fixture(const QTemporaryDir& dir)
{
    Project project;
    project.setFilePath(dir.filePath("project.txt"));
    project.setGroups({"dialogue", "outside"});
    for (int i = 0; i < 3; ++i) {
        const auto name = QStringLiteral("%1.png").arg(i);
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(QColor(80 * i, 25, 70));
        if (!image.save(dir.filePath(name)))
            qFatal("Cannot create fixture image");
        project.images().append({name, dir.filePath(name), {Label("hello hello", "dialogue", {0.2, 0.3})}});
    }
    LabelPlusDocument::saveToFile(project, project.filePath());
    return project;
}
bool captureWidget(QWidget& widget, const QString& name)
{
    const auto path = qEnvironmentVariable("LABELQT_TEST_SCREENSHOTS");
    if (path.isEmpty())
        return true;
    QCoreApplication::processEvents();
    return QDir().mkpath(path) && widget.grab().save(QDir(path).filePath(name + ".png"));
}
} // namespace

class ProductWorkflowTests final : public QObject {
    Q_OBJECT
private slots:
    void externalChangesRequireTheConfirmedRevision()
    {
        QTemporaryDir dir;
        const auto original = fixture(dir);
        ProjectController controller;
        controller.loadFromFile(original.filePath());
        controller.project().images()[0].labels[0].setText("local");
        controller.markDirty();
        auto external = original;
        external.images()[0].labels[0].setText("external");
        LabelPlusDocument::saveToFile(external, external.filePath());
        const auto revision = FileRevision::read(external.filePath());
        QVERIFY(revision);
        QVERIFY_EXCEPTION_THROWN(controller.save(), ProjectFileConflict);
        QVERIFY(controller.isDirty());
        QCOMPARE(LabelPlusDocument::loadFromFile(original.filePath()).images()[0].labels[0].text(), "external");
        external.images()[0].labels[0].setText("newer");
        LabelPlusDocument::saveToFile(external, external.filePath());
        QVERIFY_EXCEPTION_THROWN(controller.save(*revision), ProjectFileConflict);
        controller.save(*FileRevision::read(external.filePath()));
        QVERIFY(!controller.isDirty());
        QCOMPARE(LabelPlusDocument::loadFromFile(original.filePath()).images()[0].labels[0].text(), "local");
    }

    void saveAsAndRecoveryKeepImageAndSourceReferences()
    {
        QTemporaryDir dir, elsewhere, recovered;
        auto project = fixture(dir);
        PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(
            project, {{project.images()[0].name, {0, dir.filePath("source.txt")}}});
        ProjectController controller;
        controller.setProject(project);
        controller.saveAs(elsewhere.filePath("copy.txt"));
        const auto copy = LabelPlusDocument::loadFromFile(controller.project().filePath());
        QCOMPARE(copy.images()[0].path, project.images()[0].path);
        QCOMPARE(
            QDir::cleanPath(PageSourceInfoService::sourcesForProject(copy).value(project.images()[0].name).sourcePath),
            dir.filePath("source.txt"));
        const auto backup = elsewhere.filePath("backup.txt");
        QVERIFY(ProjectRecoveryService::writeBackup(project, backup));
        auto values = AppPreferences{}.values();
        values.backupPath = elsewhere.path();
        const auto records = ProjectRecoveryService::entries(project.filePath(), AppPreferences::fromValues(values));
        QCOMPARE(records.size(), 1);
        QVERIFY(ProjectRecoveryService::restoreCopy(records.first(), recovered.filePath("recovered.txt")));
        QCOMPARE(LabelPlusDocument::loadFromFile(recovered.filePath("recovered.txt")).images()[0].path,
                 project.images()[0].path);
        QVERIFY(!ProjectRecoveryService::restoreCopy(records.first(), project.filePath()));
        QVERIFY(AtomicFileWriter::write(backup, "changed"));
        QVERIFY(!ProjectRecoveryService::preview(records.first()));
    }

    void renameRecovery_data()
    {
        QTest::addColumn<bool>("complete");
        QTest::newRow("restore-original") << false;
        QTest::newRow("finish-operation") << true;
    }
    void renameRecovery()
    {
        QFETCH(bool, complete);
        QTemporaryDir dir;
        auto project = fixture(dir);
        auto renamed = project;
        QVector<FileRenameTransaction::Move> moves;
        for (int i = 0; i < 3; ++i) {
            renamed.images()[i].name = project.images()[(i + 1) % 3].name;
            renamed.images()[i].path = project.images()[(i + 1) % 3].path;
            moves.append({project.images()[i].path, renamed.images()[i].path});
        }
        const auto firstImage = bytes(project.images()[0].path);
        FileRenameTransaction transaction(moves, dir.path());
        transaction.setProjectCommit(project.filePath(), LabelPlusDocument::serialize(project).toUtf8(),
                                     LabelPlusDocument::serialize(renamed).toUtf8());
        QVERIFY(transaction.apply());
        const auto record = bytes(transaction.recoveryPath());
        transaction.commit();
        QVERIFY(AtomicFileWriter::write(transaction.recoveryPath(), record));
        QVERIFY(ProjectRecoveryService::recoverFiles(transaction.recoveryPath(), complete));
        const auto loaded = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(loaded.images()[0].name, complete ? renamed.images()[0].name : project.images()[0].name);
        QCOMPARE(bytes(loaded.images()[0].path), firstImage);
        QVERIFY(!QFileInfo::exists(transaction.recoveryPath()));
    }

    void recoveryRejectsModifiedFileWithoutMovingAnything()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        auto renamed = project;
        renamed.images()[0].name = "new.png";
        renamed.images()[0].path = dir.filePath("new.png");
        FileRenameTransaction transaction({{project.images()[0].path, renamed.images()[0].path}}, dir.path());
        transaction.setProjectCommit(project.filePath(), LabelPlusDocument::serialize(project).toUtf8(),
                                     LabelPlusDocument::serialize(renamed).toUtf8());
        QVERIFY(transaction.apply());
        const auto record = bytes(transaction.recoveryPath());
        transaction.commit();
        QVERIFY(AtomicFileWriter::write(transaction.recoveryPath(), record));
        QVERIFY(AtomicFileWriter::write(renamed.images()[0].path, "external"));
        QVERIFY(!ProjectRecoveryService::recoverFiles(transaction.recoveryPath(), false));
        QCOMPARE(bytes(renamed.images()[0].path), QByteArray("external"));
        QVERIFY(QFileInfo::exists(transaction.recoveryPath()));
    }

    void pageDraftPreservesTargetsAndPrunesRemovedPages()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        PageOrderListModel model(project);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto plan = ProjectImageRenameService::createPlan(project, {"cover", 1, {0}});
        model.setPendingRenamePlan(plan);
        model.moveSourceIndexes({0}, 1);
        QCOMPARE(model.draft().pendingItem(0).newName, "cover.png");
        model.removeSourceIndexes({0});
        QVERIFY(!model.draft().pending().hasChanges());
        model.undoStack().undo();
        QVERIFY(model.draft().pendingItem(0).isChanged());
        QCOMPARE(model.pageOrder(), QVector<int>({1, 0, 2}));
        model.clearPendingRenamePlan();
        QVERIFY(!model.draft().pending().hasChanges());
    }

    void removingTrailingPagesAndCombinedRenameAreCommitted()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        auto removed = project;
        ProjectPageOrderService::reorderImages(removed, {0});
        QCOMPARE(removed.images().size(), 1);
        ProjectPageOrderService::reorderImages(removed, {});
        QVERIFY(removed.images().isEmpty());
        ProjectController controller;
        controller.setProject(project);
        auto plan = ProjectImageRenameService::createPlan(project, {"cover", 1, {0}});
        plan.finalPageOrder = QVector<int>{2, 0};
        QVERIFY(controller.renameImages(plan));
        const auto loaded = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(loaded.images().size(), 2);
        QCOMPARE(loaded.images()[0].name, "2.png");
        QCOMPARE(loaded.images()[1].name, "cover.png");
        QCOMPARE(controller.project().images()[1].path, loaded.images()[1].path);
        QVERIFY(!controller.isDirty());
    }

    void missingPageCanReferenceAnotherDirectoryAndExtension()
    {
        QTemporaryDir dir, images;
        auto project = fixture(dir);
        project.images()[0].path = dir.filePath("missing.jpg");
        project.images()[0].name = "missing.jpg";
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(images.filePath("missing.png")));
        QCOMPARE(ProjectImageRenameService::referenceCandidates(project.images()[0].path, images.path()).size(), 1);
        const auto plan = ProjectImageRenameService::referencePlan(project, {{0, images.filePath("missing.png")}});
        QVERIFY(plan.isValid());
        QVERIFY(plan.items[0].referenceOnly);
        QVERIFY(ProjectImageRenameService::applyPlan(project, plan));
        QCOMPARE(project.images()[0].path, images.filePath("missing.png"));
        QVERIFY(QFileInfo::exists(images.filePath("missing.png")));
    }

    void textSearchReplaceUsesOneUndoWithoutSelectionChanges()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        project.images()[0].labels[0].setText(QString::fromUtf8("猫😀\nhello HELLO"));
        ProjectTextQuery query;
        query.text = "hello";
        const auto result = ProjectTextSearchService::search(project, query);
        QCOMPARE(result.matches.size(), 6);
        QCOMPARE(result.matches[0].start, 4);
        ProjectTextSearchModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setResult(result);
        QCOMPARE(model.rowCount(), 6);
        const auto edits = ProjectTextSearchService::replacements(result, {0, 1, 4}, QStringLiteral("Hi\nthere"));
        UndoStack undo;
        LabelEditController controller(project, undo, {});
        int selected = 0;
        controller.setCallbacks([&](int, int) { ++selected; }, {}, {}, {}, {});
        QVERIFY(controller.replaceTexts(result.revision, edits, "replace").value());
        QCOMPARE(project.images()[0].labels[0].text(), QString::fromUtf8("猫😀\nHi\nthere Hi\nthere"));
        QCOMPARE(selected, 0);
        undo.undo();
        QCOMPARE(project.images()[0].labels[0].text(), result.matches[0].text);
        QVERIFY(!undo.canUndo());
        undo.redo();
        QCOMPARE(selected, 0);
        QVERIFY(!controller.replaceTexts(result.revision, edits, "stale"));
        query.groups = {"outside"};
        query.allGroups = false;
        QVERIFY(ProjectTextSearchService::search(project, query).matches.isEmpty());
    }

    void manualPairsValidateIdentityAndReviewFingerprints()
    {
        QTemporaryDir dir;
        auto before = fixture(dir);
        auto after = before;
        before.images()[0].labels[0].setText("before");
        after.images()[0].labels[0].setText("after");
        const auto plan = ProjectComparisonService::planComparison(before, after);
        const auto baseline = plan.baselineMetadata.baselineFor("0.png", 0);
        const auto current = ProjectComparisonService::captureSnapshot(after).baselineFor("0.png", 0);
        const QVector<ManualLabelPair> pairs{{baseline, current}};
        const auto changes =
            ProjectComparisonService::changesWithPairs(after, plan.baselineMetadata, plan.pagePairs, pairs);
        QVERIFY(changes);
        QVERIFY(changes->first().manuallyMatched);
        QVERIFY(!ProjectComparisonService::changesWithPairs(after, plan.baselineMetadata, plan.pagePairs,
                                                            {pairs.first(), pairs.first()}));
        ReviewSession session;
        session.pairs = pairs;
        const auto key = ReviewSessionService::fingerprint(changes->first());
        session.annotations.insert(key, {ReviewStatus::Reviewed, "<script>note</script>"});
        const auto restored = ReviewSessionService::decode(ReviewSessionService::encode(session));
        QVERIFY(restored);
        QCOMPARE(restored->annotations.value(key).status, ReviewStatus::Reviewed);
        auto changed = changes->first();
        changed.current.text += " edited";
        QVERIFY(ReviewSessionService::fingerprint(changed) != key);
        after.images()[0].labels[0].setText("new edit");
        QVERIFY(!ProjectComparisonService::changesWithPairs(after, plan.baselineMetadata, plan.pagePairs, pairs));
        ProofreadReportTexts texts;
        texts.review = "Review";
        texts.reviewStates = {"Unseen", "Reviewed", "Needs confirmation"};
        ProofreadReportOptions options;
        options.includeReview = true;
        options.annotations = session.annotations;
        const auto html = ProofreadReportService::htmlReport(*changes, before, before, texts, {}, options);
        QVERIFY(html.contains("Reviewed"));
        QVERIFY(html.contains("&lt;script&gt;"));
        QVERIFY(!html.contains("<script>"));
    }
    void saveAsThenUndoPageOrderPreservesSourcePaths()
    {
        QTemporaryDir dir, elsewhere;
        auto project = fixture(dir);
        const QString source = dir.filePath("source.txt");
        PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(project,
                                                                       {{project.images()[0].name, {0, source}}});
        ProjectController controller;
        controller.setProject(project);
        UndoStack undo;
        ProjectWorkflowController workflow(controller, undo, "Reorder");
        workflow.setCallbacks(
            [&](QVector<ImageEntry> images, const QString&, int, int, QPointF, std::optional<QStringList> comments) {
                controller.project().images() = std::move(images);
                if (comments)
                    controller.project().setCommentLines(*comments);
            },
            [&] { controller.markDirty(); });
        QVERIFY(workflow.applyPageOrder({2, 1, 0}, {}));
        controller.saveAs(elsewhere.filePath("copy.txt"));
        undo.undo();
        QCOMPARE(QDir::cleanPath(PageSourceInfoService::sourcesForProject(controller.project())
                                     .value(project.images()[0].name)
                                     .sourcePath),
                 source);
        controller.save();
        undo.redo();
        QCOMPARE(QDir::cleanPath(PageSourceInfoService::sourcesForProject(controller.project())
                                     .value(project.images()[0].name)
                                     .sourcePath),
                 source);
        QCOMPARE(controller.project().images().first().name, project.images().last().name);
    }

    void backupRetentionKeepsManualAndUnverifiedFiles()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        auto values = AppPreferences{}.values();
        values.backupPath = dir.path();
        values.backupMaximumCount = 1;
        const auto preferences = AppPreferences::fromValues(values);
        for (int index = 0; index < 3; ++index)
            QVERIFY(ProjectRecoveryService::writeBackup(project, dir.filePath(QStringLiteral("auto%1.txt").arg(index)),
                                                        true));
        QVERIFY(ProjectRecoveryService::writeBackup(project, dir.filePath("manual.txt")));
        QVERIFY(ProjectRecoveryService::writeBackup(project, dir.filePath("edited.txt"), true));
        QVERIFY(AtomicFileWriter::write(dir.filePath("edited.txt"), "user changed backup"));
        ProjectRecoveryService::pruneAutomaticBackups(project.filePath(), preferences);
        QVERIFY(QFileInfo::exists(dir.filePath("manual.txt")));
        QCOMPARE(bytes(dir.filePath("edited.txt")), "user changed backup");
        const auto records = ProjectRecoveryService::entries(project.filePath(), preferences);
        QVERIFY(records.size() >= 2 && records.size() <= 3);
        QVERIFY(!AtomicFileWriter::writeNew(project.filePath(), "must not overwrite"));
        QCOMPARE(LabelPlusDocument::loadFromFile(project.filePath()).images().size(), 3);
        const auto roundTrip = AppPreferences::loadFromJson(preferences.toJsonDocument().toJson());
        QCOMPARE(roundTrip.preferences.backupMaximumCount(), 1);
        QVERIFY(preferences.conflictingSearchShortcuts().isEmpty());
        values.findProjectShortcut = QKeySequence::Copy;
        const auto conflict = AppPreferences::fromValues(values);
        QVERIFY(conflict.conflictingSearchShortcuts().contains("findProjectShortcut"));
    }

    void removedPageDoesNotReserveAReferenceButStillProtectsItsFile()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        project.images()[0].path = dir.filePath("missing.png");
        PageManagementDraft draft(project);
        draft.setOrder({0, 1});
        const auto reference = ProjectImageRenameService::referencePlan(project, {{0, project.images()[2].path}});
        QVERIFY(draft.queue(reference));
        QCOMPARE(draft.pendingItem(0).newName, project.images()[2].name);
        QVERIFY(QFileInfo::exists(project.images()[2].path));
        const auto rename = ProjectImageRenameService::createPlan(project, {"2", 1, {1}});
        QVERIFY(!draft.preview(rename).isValid());
    }

    void searchPanelTracksCurrentPageWithoutImplicitNavigation()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        int page = 0, navigations = 0;
        QWidget owner;
        ProjectSearchPanel panel({[&]() -> const Project& { return project; }, [&] { return page; },
                                  [] { return false; }, [] {}, [&](const ProjectTextMatch&) { ++navigations; }, [] {},
                                  [](const QByteArray&, QVector<LabelTextReplacement>,
                                     const QString&) -> std::expected<bool, QString> { return false; }},
                                 &owner);
        panel.actions().first()->trigger();
        auto* query = panel.findChildren<QPlainTextEdit*>().first();
        auto* table = panel.findChild<QTableView*>();
        QAbstractItemModelTester tester(table->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        query->setPlainText("hello");
        QTRY_COMPARE(table->model()->rowCount(), 6);
        panel.resize(720, panel.sizeHint().height());
        QVERIFY(captureWidget(panel, "search"));
        QCOMPARE(navigations, 0);
        auto* scope = panel.findChild<QComboBox*>();
        scope->setCurrentIndex(1);
        QTRY_COMPARE(table->model()->rowCount(), 2);
        page = 2;
        panel.currentPageChanged();
        QTRY_COMPARE(table->model()->index(0, 0).data().toString(), project.images()[2].name);
        QCOMPARE(navigations, 0);
        panel.actions().at(1)->trigger();
        QCOMPARE(navigations, 1);
        page = -1;
        panel.currentPageChanged();
        QTRY_COMPARE(table->model()->rowCount(), 0);
    }

    void pageDialogKeepsDraftOnRejectedCommit()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        PageOrderDialog dialog(project, AppPreferences{}, nullptr, true);
        auto* table = dialog.findChild<QTableView*>();
        QVERIFY(table);
        auto* model = dynamic_cast<PageOrderListModel*>(table->model());
        QVERIFY(model);
        QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model->setPendingRenamePlan(ProjectImageRenameService::createPlan(project, {"cover", 1, {0}}));
        table->selectRow(2);
        QCOMPARE(model->index(0, PageOrderListModel::RenamePreviewColumn).data().toString(), "cover.png");
        model->setPendingRenamePlan(ProjectImageRenameService::createPlan(project, {"last", 1, {2}}));
        QCOMPARE(model->index(0, PageOrderListModel::RenamePreviewColumn).data().toString(), "cover.png");
        int commits = 0;
        bool hasOrder = false;
        dialog.setPendingImageRenameHandler([&](const ProjectImageRenamePlan& plan) {
            ++commits;
            hasOrder = plan.finalPageOrder.has_value();
            return false;
        });
        dialog.showNormal();
        dialog.resize(1100, 720);
        QVERIFY(captureWidget(dialog, "pages"));
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(commits, 1);
        QVERIFY(hasOrder);
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.hasPendingImageRenamePlan());
        QVERIFY(QFileInfo::exists(project.images()[0].path));
        dialog.reject();
    }

    void reviewSelectionDoesNotMarkAndUndoRestoresNotes()
    {
        ReviewChange change;
        change.imageName = "page.png";
        change.current.text = "new";
        ProofreadReviewPanel panel(
            {[&] { return QVector<ReviewChange>{change}; }, [&] { return QVector<ReviewChange>{change}; },
             [](const QVector<ManualLabelPair>&) -> std::expected<void, QString> { return {}; }});
        panel.setCurrentChange(&change);
        QVERIFY(panel.session().annotations.isEmpty());
        QVERIFY(!panel.findChild<QPlainTextEdit*>());
        const auto key = ReviewSessionService::fingerprint(change);
        auto saved = panel.session();
        saved.annotations[key].note = "saved note";
        panel.setSession(saved);
        QCOMPARE(panel.session().annotations.value(key).note, "saved note");
        auto* status = panel.findChild<QComboBox*>();
        status->setCurrentIndex(1);
        QVERIFY(QMetaObject::invokeMethod(status, "activated", Q_ARG(int, 1)));
        QCOMPARE(panel.session().annotations.value(key).status, ReviewStatus::Reviewed);
        panel.findChild<QToolButton*>("undoReview")->click();
        QCOMPARE(panel.session().annotations.value(key).status, ReviewStatus::Unseen);
        QCOMPARE(panel.session().annotations.value(key).note, "saved note");
        panel.findChild<QToolButton*>("redoReview")->click();
        QCOMPARE(panel.session().annotations.value(key).status, ReviewStatus::Reviewed);
    }

    void reviewDialogPreservesStateAndHasNoImplicitEdits()
    {
        QTemporaryDir dir;
        auto before = fixture(dir), after = before;
        after.images()[0].labels[0].setText("changed");
        const auto plan = ProjectComparisonService::planComparison(before, after);
        const auto changes = ProjectComparisonService::changesBetweenProjects(after, plan);
        ProofreadChangesDialogLabels labels;
        labels.reviewEnabled = true;
        labels.projectReviewSession = true;
        ProofreadChangesDialog dialog(before, after, AppPreferences{}, plan.baselineMetadata, changes, labels);
        dialog.showNormal();
        QCoreApplication::processEvents();
        dialog.resize(1200, 780);
        QVERIFY(captureWidget(dialog, "review"));
        auto* panel = dialog.findChild<ProofreadReviewPanel*>();
        QVERIFY(panel);
        QVERIFY(panel->session().annotations.isEmpty());
        auto* status = panel->findChild<QComboBox*>();
        QVERIFY(QMetaObject::invokeMethod(status, "activated", Q_ARG(int, 1)));
        QCoreApplication::processEvents();
        QCOMPARE(panel->session().annotations.value(ReviewSessionService::fingerprint(changes.first())).status,
                 ReviewStatus::Reviewed);
        auto* table = dialog.findChild<QTableView*>();
        QVERIFY(table);
        const auto reviewIndex = table->model()->index(0, ProofreadChangeTableModel::ReviewColumn);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(reviewIndex).center());
        QCOMPARE(panel->session().annotations.value(ReviewSessionService::fingerprint(changes.first())).status,
                 ReviewStatus::Unseen);
        panel->setEnabled(false);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(reviewIndex).center());
        QCOMPARE(panel->session().annotations.value(ReviewSessionService::fingerprint(changes.first())).status,
                 ReviewStatus::Unseen);
        dialog.reject();
        QVERIFY(!dialog.reviewSession().value("annotations").toObject().isEmpty());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ProductWorkflowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ProductWorkflowTests.moc"
