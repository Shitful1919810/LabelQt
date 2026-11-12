#include "core/LabelPlusDocument.h"
#include "mobile/MobileReviewController.h"
#include "mobile/MobileWorkspaceController.h"
#include "services/transfer/TransferSupport.h"

#include <QAbstractItemModelTester>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace labelqt::mobile;
using namespace labelqt::services;

class MobileWorkspaceTests : public QObject {
    Q_OBJECT

    static QString createInput(const QString& directory)
    {
        labelqt::core::Project project;
        project.setFilePath(QDir(directory).filePath("translation.txt"));
        project.setGroups({QStringLiteral("Dialogue")});
        labelqt::core::ImageEntry page;
        page.name = QStringLiteral("page.png");
        page.path = QDir(directory).filePath(page.name);
        page.labels.append(labelqt::core::Label(QStringLiteral("Original"), QStringLiteral("Dialogue"), {0.5, 0.5}));
        QImage image(100, 150, QImage::Format_RGB32);
        image.fill(Qt::white);
        if (!image.save(page.path))
            qFatal("Cannot write fixture image");
        project.images().append(page);
        QFile file(project.filePath());
        if (!file.open(QIODevice::WriteOnly))
            qFatal("Cannot write fixture TXT");
        const auto bytes = labelqt::core::LabelPlusDocument::serialize(project).toUtf8();
        if (file.write(bytes) != bytes.size())
            qFatal("Cannot finish fixture TXT");
        return project.filePath();
    }

private slots:
    void reshareChecksPublishedFileWithoutSavingOrExporting_data()
    {
        QTest::addColumn<bool>("pdf");
        QTest::addColumn<QString>("invalidatedBy");
        QTest::newRow("zip-removed") << false << QString("removed");
        QTest::newRow("pdf-expired") << true << QString("expired");
        QTest::newRow("zip-empty") << false << QString("empty");
#ifdef Q_OS_UNIX
        QTest::newRow("pdf-symlink") << true << QString("symlink");
        QTest::newRow("zip-parent-symlink") << false << QString("parent-symlink");
#endif
    }

    void reshareChecksPublishedFileWithoutSavingOrExporting()
    {
        QFETCH(bool, pdf);
        QFETCH(QString, invalidatedBy);
        QTemporaryDir root;
        const auto path = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        QVERIFY(!workspace.shareLastExport());
        workspace.importLocal(QUrl::fromLocalFile(path));
        QTRY_VERIFY(!workspace.busy());
        QVERIFY(workspace.openWorkspace(workspace.workspaces()->entries().first().id));
        MobileReviewController review(document.controller(), document.undoStack());
        QVERIFY(review.startRound());
        QVERIFY(review.addNote(document.pageId(), {}, "Report fixture comment"));
        QSignalSpy ready(&workspace, &MobileWorkspaceController::exportReady);
        QVERIFY(pdf ? workspace.exportProofreadingPdf("report", false) : workspace.exportProject("project"));
        QVERIFY(!workspace.shareLastExport());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QVERIFY2(!workspace.lastExport().isEmpty(), qPrintable(workspace.message()));
        QCOMPARE(ready.count(), 1);
        const auto exported = workspace.lastExport();
        QVERIFY(!exported.isEmpty());
        WorkspaceStorageService storage(root.filePath("app"));
        const auto validated = storage.validateSharedFile(exported);
        QVERIFY(validated);
        QCOMPARE(*validated, exported);
        QVERIFY(!storage.validateSharedFile(path));
        const auto wrongSuffix = QFileInfo(exported).dir().filePath("private.txt");
        QVERIFY(QFile::copy(exported, wrongSuffix));
        QVERIFY(!storage.validateSharedFile(wrongSuffix));
        QVERIFY(QFile::remove(wrongSuffix));
        const auto invalidBatch = root.filePath("app/outgoing/not-a-uuid");
        QVERIFY(QDir().mkpath(invalidBatch));
        const auto misplaced = QDir(invalidBatch).filePath("report.pdf");
        QVERIFY(QFile::copy(exported, misplaced));
        QVERIFY(!storage.validateSharedFile(misplaced));
        QVERIFY(QFile::remove(misplaced));
        QVERIFY(QDir().rmdir(invalidBatch));
        QFile original(exported);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();
        const auto modified = QFileInfo(exported).lastModified();
        const auto revision = document.controller().revisions().revision();
        document.selectLabel(document.controller().project().images()[0].labels[0].id().toString());
        document.updateText("Uncommitted newer draft");
        workspace.showMessage("Share chooser unavailable");
        QVERIFY(workspace.shareLastExport());
        QCOMPARE(ready.count(), 2);
        QCOMPARE(ready.last().first().toString(), exported);
        QVERIFY(!workspace.busy());
        QVERIFY(workspace.message().isEmpty());
        QCOMPARE(document.controller().revisions().revision(), revision);
        QCOMPARE(document.controller().project().images()[0].labels[0].text(), "Original");
        QCOMPARE(document.textDraft(), "Uncommitted newer draft");
        QCOMPARE(QFileInfo(exported).lastModified(), modified);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
        original.close();
        if (invalidatedBy == "expired") {
            QVERIFY(WorkspaceStorageService(root.filePath("app")).cleanDisposableFiles(
                QDateTime::currentDateTimeUtc().addDays(8)));
            QVERIFY(!QFileInfo::exists(exported));
        }
        else if (invalidatedBy == "parent-symlink") {
            const QString batch = QFileInfo(exported).dir().absolutePath();
            const auto moved = root.filePath("moved-batch");
            QVERIFY(QDir().rename(batch, moved));
            QVERIFY(QFile::link(moved, batch));
        }
        else {
            QVERIFY(QFile::remove(exported));
            if (invalidatedBy == "symlink")
                QVERIFY(QFile::link(path, exported));
            else if (invalidatedBy == "empty") {
                QFile empty(exported);
                QVERIFY(empty.open(QIODevice::WriteOnly));
            }
        }
        QVERIFY(!workspace.shareLastExport());
        QCOMPARE(ready.count(), 2);
        QVERIFY(!storage.validateSharedFile(exported));
        QVERIFY(workspace.lastExport().isEmpty());
        QVERIFY(!workspace.message().isEmpty());
    }

    void classicProjectDeleteEditAndZipRoundTrip()
    {
        QTemporaryDir root;
        const auto input = createInput(root.path());
        const QByteArray classic = "1,0\n-\ndialogue\nsound\n-\n\n>>>>>>>>[page.png]<<<<<<<<\n"
                                   "----------------[1]----------------[0.200,0.300,1]\nfirst\n\n"
                                   "----------------[2]----------------[0.400,0.500,2]\nsecond\n\n"
                                   "----------------[3]----------------[0.600,0.700,1]\nthird\n\n";
        QFile source(input);
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(source.write(classic), classic.size());
        source.close();
        ProjectTransferService transfer(root.filePath("app"));
        const auto imported = transfer.importFile(input);
        QVERIFY(imported.has_value());
        MobileDocument mobile;
        QVERIFY(mobile.openProject(imported->projectPath));
        auto* model = qobject_cast<MobileLabelModel*>(mobile.labels());
        QVERIFY(model);
        const auto third = model->index(2).data(MobileLabelModel::Key).toString();
        mobile.selectLabel(model->index(1).data(MobileLabelModel::Key).toString());
        mobile.deleteSelected();
        mobile.selectLabel(third);
        mobile.updateText("third edited");
        mobile.setVisibleGroups({"sound"}); // Empty view must not become an empty saved project.
        QCOMPARE(model->rowCount(), 0);
        QVERIFY(mobile.save());
        const auto saved = labelqt::core::LabelPlusDocument::loadFromFile(imported->projectPath);
        QCOMPARE(saved.images()[0].labels.size(), 2);
        QCOMPARE(saved.images()[0].labels[1].text(), "third edited");
        const auto text = labelqt::core::LabelPlusDocument::serialize(saved);
        QVERIFY(text.contains("----------------[2]----------------[0.600,0.700,1]"));
        QVERIFY(!text.contains("----------------[3]----------------"));
        // The standard body remains readable without any LabelQt comment metadata.
        const auto legacyBody = QStringLiteral("1,0\n-\ndialogue\nsound\n-\n\n") + text.mid(text.indexOf(">>>>>>>>["));
        const auto legacy = labelqt::core::LabelPlusDocument::parse(legacyBody, root.filePath("legacy.txt"));
        QCOMPARE(legacy.images()[0].labels.size(), 2);
        QCOMPARE(legacy.images()[0].labels[1].text(), "third edited");
        QCOMPARE(legacy.images()[0].labels[1].group(), "dialogue");
        const auto exported = transfer.exportZip(saved);
        QVERIFY(exported.has_value());
        const auto received = ProjectTransferService(root.filePath("recipient")).importFile(*exported);
        QVERIFY(received.has_value());
        MobileDocument recipient;
        QVERIFY(recipient.openProject(received->projectPath));
        auto* receivedModel = qobject_cast<MobileLabelModel*>(recipient.labels());
        QVERIFY(receivedModel);
        QCOMPARE(receivedModel->rowCount(), 2);
        QCOMPARE(receivedModel->index(1).data(MobileLabelModel::Number).toInt(), 2);
        QCOMPARE(receivedModel->index(1).data(MobileLabelModel::Key).toString(), third);
        QCOMPARE(receivedModel->index(1).data(MobileLabelModel::Text).toString(), "third edited");
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), classic);
    }

    void imageArchiveCreatesEditableWorkspace()
    {
        QTemporaryDir root;
        const auto source = root.filePath("source");
        QVERIFY(QDir().mkpath(source));
        QImage image(100, 150, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(QDir(source).filePath("page.png")));
        const auto input = root.filePath("raw-images.zip");
        transfer::writeZip(source, {"page.png"}, input, {});
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(input));
        QTRY_COMPARE_WITH_TIMEOUT(workspace.workspaces()->rowCount(), 1, 10000);
        QTRY_VERIFY(!workspace.busy());
        QVERIFY(workspace.openWorkspace(workspace.workspaces()->entries().first().id));
        QCOMPARE(document.title(), "raw-images");
        QVERIFY(!document.groups().isEmpty());
        document.addLabel(0.5, 0.5);
        QVERIFY(!document.selectedLabel().isEmpty());
        document.updateText("New translation");
        QVERIFY(document.save());
        document.undo();
        QCOMPARE(document.controller().project().images()[0].labels[0].text(), QString{});
        document.redo();
        QCOMPARE(document.controller().project().images()[0].labels[0].text(), "New translation");
        QVERIFY(workspace.exportProject(workspace.suggestedShareName()));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QVERIFY2(!workspace.lastExport().isEmpty(), qPrintable(workspace.message()));
        const auto copy = ProjectTransferService(root.filePath("recipient")).importFile(workspace.lastExport());
        QVERIFY(copy.has_value());
        QCOMPARE(labelqt::core::LabelPlusDocument::loadFromFile(copy->projectPath).images()[0].labels[0].text(),
                 "New translation");
    }

    void queuedImportsAreIndependentAndNeverOverwriteSource()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(path));
        workspace.importLocal(QUrl::fromLocalFile(path));
        QAbstractItemModelTester modelTester(workspace.workspaces(),
                                             QAbstractItemModelTester::FailureReportingMode::QtTest);
        QTRY_COMPARE_WITH_TIMEOUT(workspace.workspaces()->rowCount(), 2, 10000);
        QTRY_VERIFY(!workspace.busy());
        QVERIFY(!document.opened());
        const auto first = workspace.workspaces()->entries().first().id;
        const auto second = workspace.workspaces()->entries().last().id;
        QVERIFY(first != second);
        QVERIFY(workspace.openWorkspace(first));
        QCOMPARE(document.title(), QStringLiteral("translation"));
        const auto& label = document.controller().project().images().first().labels.first();
        document.selectLabel(label.id().toString());
        document.updateText(QStringLiteral("Edited copy"));
        QVERIFY(document.save());
        ProjectController original;
        original.loadFromFile(path);
        QCOMPARE(original.project().images().first().labels.first().text(), QStringLiteral("Original"));
        QVERIFY(workspace.openWorkspace(second));
        QCOMPARE(document.controller().project().images().first().labels.first().text(), QStringLiteral("Original"));
    }

    void exportUsesCapturedSnapshotAndAllowsFurtherEdits()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(path));
        QTRY_COMPARE_WITH_TIMEOUT(workspace.workspaces()->rowCount(), 1, 10000);
        QVERIFY(workspace.openWorkspace(workspace.workspaces()->entries().first().id));
        MobileReviewController review(document.controller(), document.undoStack());
        connect(&review, &MobileReviewController::edited, &document, &MobileDocument::refresh);
        review.startRound();
        QVERIFY(workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        const auto noteText = QStringLiteral("校对批注：空格、换行\n日本語 & <tag>");
        QVERIFY(review.addNote(document.pageId(), {}, noteText));
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        document.updateText(QStringLiteral("Export snapshot"));
        QVERIFY(workspace.exportProject(workspace.suggestedShareName()));
        QVERIFY(workspace.busy());
        document.updateText(QStringLiteral("Newer edit"));
        QVERIFY(document.save());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QVERIFY(workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        const auto listed = ProjectTransferService(root.filePath("app")).listWorkspaces();
        QCOMPARE(listed.size(), 1);
        QVERIFY(listed.first().reviewActive);
        QVERIFY(workspace.renameWorkspace(listed.first().id, "Renamed"));
        QVERIFY(workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        QVERIFY2(!workspace.lastExport().isEmpty(), qPrintable(workspace.message()));
        QVERIFY(workspace.message().isEmpty());
        const auto unpacked = root.filePath("shared");
        QVERIFY(QDir().mkpath(unpacked));
        transfer::extractZip(workspace.lastExport(), unpacked, {});
        const auto shared =
            labelqt::core::LabelPlusDocument::loadFromFile(QDir(unpacked).filePath("project/project.txt"));
        const auto sharedRound = ReviewRoundService::read(shared);
        QVERIFY(sharedRound && sharedRound->has_value());
        const auto localRound = ReviewRoundService::read(document.controller().project());
        QVERIFY(localRound && localRound->has_value());
        QCOMPARE((**localRound).notes.first().text, noteText);
        QCOMPARE((**sharedRound).id, (**localRound).id);
        QCOMPARE((**sharedRound).notes.first().id, (**localRound).notes.first().id);
        QCOMPARE((**sharedRound).notes.first().text, (**localRound).notes.first().text);
        QCOMPARE(shared.images().first().id, document.controller().project().images().first().id);
        QVERIFY(!(**sharedRound).after);
        ProjectTransferService recipient(root.filePath("recipient"));
        const auto imported = recipient.importFile(workspace.lastExport());
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
        ProjectController exported;
        exported.loadFromFile(imported->projectPath);
        QCOMPARE(exported.project().images().first().labels.first().text(), QStringLiteral("Export snapshot"));
        QCOMPARE(document.controller().project().images().first().labels.first().text(), QStringLiteral("Newer edit"));
        const auto received = ReviewRoundService::read(exported.project());
        QVERIFY(received && received->has_value());
        QCOMPARE((**received).notes.first().pageId, exported.project().images().first().id);
        QVERIFY(imported->reviewActive);
        review.completeRound();
        QVERIFY(!workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        document.undo();
        QVERIFY(workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        document.redo();
        QVERIFY(!workspace.workspaces()->index(0).data(MobileWorkspaceModel::ReviewActiveRole).toBool());
        review.resolveNote(review.notes().first().toMap().value("key").toString(), true);
        QVERIFY(workspace.exportProject("completed"));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        const auto completedImport = recipient.importFile(workspace.lastExport());
        QVERIFY(completedImport);
        exported.loadFromFile(completedImport->projectPath);
        const auto completed = ReviewRoundService::read(exported.project());
        QVERIFY(completed && completed->has_value() && (**completed).after);
        QVERIFY((**completed).notes.first().resolved);
        QCOMPARE((**completed).notes.first().id, (**localRound).notes.first().id);
        QCOMPARE((**completed).after->images().first().labels.first().text(), "Newer edit");
        QCOMPARE((**completed).before.images().first().labels.first().text(), "Original");
    }

    void pdfSharingUsesRoundSnapshotAndSerializedStorage()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(path));
        QTRY_VERIFY(!workspace.busy());
        QCOMPARE(workspace.workspaces()->rowCount(), 1);
        const auto id = workspace.workspaces()->entries().first().id;
        QVERIFY(workspace.openWorkspace(id));
        MobileReviewController review(document.controller(), document.undoStack());
        connect(&review, &MobileReviewController::edited, &document, &MobileDocument::refresh);
        QVERIFY(review.startRound());
        QVERIFY(review.addNote(document.pageId(), {}, "PDF comment"));
        QSignalSpy ready(&workspace, &MobileWorkspaceController::exportReady);
        QVERIFY(!workspace.exportProofreadingPdf("../invalid", false));
        QVERIFY(!workspace.busy());
        QVERIFY(workspace.suggestedReportName().endsWith(".pdf"));
        QVERIFY(workspace.exportProofreadingPdf("review", true));
        QVERIFY(workspace.reportExporting());
        QVERIFY(!workspace.deleteWorkspace(id));
        QVERIFY(!workspace.exportProject("concurrent"));
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        document.updateText("Edited during export");
        QVERIFY(document.save());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QCOMPARE(ready.size(), 1);
        QVERIFY(workspace.lastExport().endsWith("/review.pdf"));
        QFile pdf(workspace.lastExport());
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF-"));
        QVERIFY(workspace.exportProofreadingPdf("cancel", false));
        workspace.cancelReportExport();
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QCOMPARE(ready.size(), 1);
        QVERIFY(workspace.lastExport().isEmpty());
        QVERIFY(!workspace.reportExporting());
        QCOMPARE(document.controller().project().images().first().labels.first().text(), "Edited during export");
        QVERIFY(workspace.exportProject("after-pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 15000);
        QVERIFY(workspace.lastExport().endsWith(".zip"));
    }

    void receivedArchiveKeepsDisplayNameAcrossReopen()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        const auto project = labelqt::core::LabelPlusDocument::loadFromFile(path);
        const auto exported = ProjectTransferService(root.filePath("sender")).exportZip(project);
        QVERIFY(exported.has_value());
        const auto app = root.filePath("app");
        QVERIFY(QDir().mkpath(app + "/inbox"));
        const auto received = app + "/inbox/transport-id.zip";
        QVERIFY(QFile::copy(*exported, received));
        MobileDocument document;
        QString key;
        {
            MobileWorkspaceController workspace(document, app);
            workspace.importReceived(received, QStringLiteral("Chapter 02 revised.zip"));
            QTRY_COMPARE_WITH_TIMEOUT(workspace.workspaces()->rowCount(), 1, 10000);
            QTRY_VERIFY(!workspace.busy());
            const auto row = workspace.workspaces()->entries().first();
            QCOMPARE(row.title, QStringLiteral("Chapter 02 revised"));
            key = row.id;
            QVERIFY(workspace.openWorkspace(key));
            QCOMPARE(document.title(), QStringLiteral("Chapter 02 revised"));
            QVERIFY(workspace.message().isEmpty());
        }
        MobileWorkspaceController reopened(document, app);
        QTRY_COMPARE(reopened.workspaces()->rowCount(), 1);
        QTRY_VERIFY(!reopened.busy());
        QVERIFY(reopened.openWorkspace(key));
        QCOMPARE(document.title(), QStringLiteral("Chapter 02 revised"));
        QCOMPARE(reopened.suggestedShareName(), QStringLiteral("Chapter 02 revised.zip"));
        QVERIFY(!QFile::exists(received));
        QVERIFY(QFile::exists(*exported));
    }

    void invalidInputKeepsActiveProject()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        MobileDocument document;
        QVERIFY(document.openProject(path));
        const auto session = document.controller().documentSession();
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importReceived(path);
        QVERIFY(!workspace.message().isEmpty());
        QVERIFY(!workspace.busy());
        workspace.importLocal(QUrl(QStringLiteral("content://provider/unknown")));
        QVERIFY(!workspace.busy());
        workspace.importLocal(QUrl::fromLocalFile(root.filePath("missing.zip")));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QVERIFY(!workspace.message().isEmpty());
        QCOMPARE(document.controller().documentSession(), session);
        QVERIFY(QFile::exists(path));
    }

    void manageActiveProjectAndKeepShareName()
    {
        QTemporaryDir root;
        const auto source = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(source));
        QAbstractItemModelTester modelTester(workspace.workspaces(),
                                             QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(workspace.workspaces(), &QAbstractItemModel::modelReset);
        QTRY_COMPARE(workspace.workspaces()->rowCount(), 1);
        QTRY_VERIFY(!workspace.busy());
        const auto key = workspace.workspaces()->entries().first().id;
        QVERIFY(workspace.openWorkspace(key));
        const auto localPath = document.controller().project().filePath();
        const auto session = document.controller().documentSession();
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        document.updateText("pending edit");
        const auto name = workspace.suggestedShareName();
        QVERIFY(workspace.renameWorkspace(key, "Renamed"));
        QCOMPARE(document.title(), "Renamed");
        QCOMPARE(document.textDraft(), "pending edit");
        QCOMPARE(workspace.suggestedShareName(), name);
        QVERIFY(!workspace.exportProject(""));
        QVERIFY(!workspace.exportProject("../outside.zip"));
        QVERIFY(workspace.exportProject("shared edition"));
        QVERIFY(!workspace.deleteWorkspace(key));
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QCOMPARE(QFileInfo(workspace.lastExport()).fileName(), "shared edition.zip");
        QCOMPARE(workspace.suggestedShareName(), name);
        document.updateText("another pending edit");
        QVERIFY(workspace.deleteWorkspace(key));
        QVERIFY(!document.opened());
        QVERIFY(!document.canUndo());
        QVERIFY(document.selectedLabel().isEmpty());
        QVERIFY(document.controller().documentSession() != session);
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QTest::qWait(2000);
        QVERIFY(!QFileInfo::exists(localPath));
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(workspace.lastExport()));
        QCOMPARE(workspace.workspaces()->rowCount(), 0);
        QCOMPARE(resets.count(), 0);
    }

    void workspaceModelPreservesSurvivingRows()
    {
        MobileWorkspaceModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.replace({{"b", "B", {}, {}, {}}, {"d", "D", {}, {}, {}}});
        const QPersistentModelIndex kept(model.index(1));
        model.replace({{"a", "A", {}, {}, {}}, {"d", "Updated", {}, {}, {}}, {"c", "C", {}, {}, {}}});
        QVERIFY(kept.isValid());
        QCOMPARE(kept.row(), 2);
        QCOMPARE(kept.data(MobileWorkspaceModel::TitleRole).toString(), "Updated");
        model.replace({{"d", "Updated", {}, {}, {}}});
        QCOMPARE(kept.row(), 0);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        auto entries = model.entries();
        entries[0].diagnostic = "Cannot inspect <broken> project";
        model.replace(entries);
        QCOMPARE(kept.data(MobileWorkspaceModel::DiagnosticRole).toString(), entries[0].diagnostic);
        QCOMPARE(model.roleNames().value(MobileWorkspaceModel::DiagnosticRole), QByteArray("diagnostic"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(changed.first()[2].value<QList<int>>().contains(MobileWorkspaceModel::DiagnosticRole));
        entries[0].diagnostic.clear();
        model.replace(entries);
        QVERIFY(kept.data(MobileWorkspaceModel::DiagnosticRole).toString().isEmpty());
        QCOMPARE(changed.count(), 2);
        QCOMPARE(reset.count(), 0);
        model.replace({});
        QVERIFY(!kept.isValid());
    }

    void deletingOtherProjectKeepsDraftAndFailedSaveBlocksDeletion()
    {
        QTemporaryDir root;
        const auto source = createInput(root.path());
        MobileDocument document;
        MobileWorkspaceController workspace(document, root.filePath("app"));
        workspace.importLocal(QUrl::fromLocalFile(source));
        workspace.importLocal(QUrl::fromLocalFile(source));
        QTRY_COMPARE(workspace.workspaces()->rowCount(), 2);
        QTRY_VERIFY(!workspace.busy());
        const auto first = workspace.workspaces()->entries().first().id;
        const auto second = workspace.workspaces()->entries().last().id;
        QVERIFY(workspace.openWorkspace(first));
        const auto path = document.controller().project().filePath();
        const auto session = document.controller().documentSession();
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        document.updateText("keep my draft");
        QVERIFY(workspace.deleteWorkspace(second));
        QCOMPARE(document.textDraft(), "keep my draft");
        QCOMPARE(document.controller().documentSession(), session);
        QTRY_VERIFY(!workspace.busy());
        QVERIFY(document.commitText());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("external change") > 0);
        file.close();
        QVERIFY(!workspace.deleteWorkspace(first));
        QVERIFY(document.opened());
        QCOMPARE(document.textDraft(), "keep my draft");
        QCOMPARE(workspace.workspaces()->rowCount(), 1);
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(!document.error().isEmpty());
    }

    void destroyingAdapterDoesNotDestroyImportSource()
    {
        QTemporaryDir root;
        const auto path = createInput(root.path());
        {
            MobileDocument document;
            MobileWorkspaceController workspace(document, root.filePath("app"));
            workspace.importLocal(QUrl::fromLocalFile(path));
        }
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
        QVERIFY(QFile::exists(path));
        QCOMPARE(ProjectTransferService(root.filePath("app")).listWorkspaces().size(), 1);
    }
};

QTEST_MAIN(MobileWorkspaceTests)
#include "MobileWorkspaceTests.moc"
