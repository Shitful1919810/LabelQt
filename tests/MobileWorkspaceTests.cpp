#include "core/LabelPlusDocument.h"
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
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        document.updateText(QStringLiteral("Export snapshot"));
        QVERIFY(workspace.exportProject(workspace.suggestedShareName()));
        QVERIFY(workspace.busy());
        document.updateText(QStringLiteral("Newer edit"));
        QVERIFY(document.save());
        QTRY_VERIFY_WITH_TIMEOUT(!workspace.busy(), 10000);
        QVERIFY2(!workspace.lastExport().isEmpty(), qPrintable(workspace.message()));
        QVERIFY(workspace.message().isEmpty());
        ProjectTransferService recipient(root.filePath("recipient"));
        const auto imported = recipient.importFile(workspace.lastExport());
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
        ProjectController exported;
        exported.loadFromFile(imported->projectPath);
        QCOMPARE(exported.project().images().first().labels.first().text(), QStringLiteral("Export snapshot"));
        QCOMPARE(document.controller().project().images().first().labels.first().text(), QStringLiteral("Newer edit"));
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

QTEST_GUILESS_MAIN(MobileWorkspaceTests)
#include "MobileWorkspaceTests.moc"
