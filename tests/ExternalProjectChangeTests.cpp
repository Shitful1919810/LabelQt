#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/ProjectFileMonitor.h"
#include "services/project/ProjectController.h"
#include "services/project/ProjectRecoveryService.h"
#include "services/project/SessionStateStore.h"
#include "ui/project/ProjectExternalChangeController.h"
#include "ui/project/ProjectRecoveryController.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
Project fixture(const QString& path)
{
    Project project;
    project.setFilePath(path);
    project.setGroups({QStringLiteral("group")});
    project.images().append(
        {QStringLiteral("page.png"), {}, {Label(QStringLiteral("original"), QStringLiteral("group"), {})}});
    return project;
}
bool writeProject(const Project& project)
{
    return bool(AtomicFileWriter::write(project.filePath(), LabelPlusDocument::serialize(project).toUtf8()));
}
void edit(Project& project, const QString& text)
{
    project.images()[0].labels[0].setText(text);
}
} // namespace

class ExternalProjectChangeTests final : public QObject {
    Q_OBJECT
private slots:
    void parsedDocumentDigestUsesOriginalBytes_data()
    {
        QTest::addColumn<QByteArray>("encoding");
        QTest::newRow("utf8") << QByteArray("utf8");
        QTest::newRow("utf8-bom-crlf") << QByteArray("bom");
        QTest::newRow("utf16le-bom") << QByteArray("utf16");
    }

    void parsedDocumentDigestUsesOriginalBytes()
    {
        QFETCH(QByteArray, encoding);
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        const auto text = LabelPlusDocument::serialize(project);
        QByteArray bytes = text.toUtf8();
        if (encoding == "bom") {
            bytes.replace("\n", "\r\n");
            bytes.prepend(QByteArray::fromHex("efbbbf"));
        }
        if (encoding == "utf16") {
            bytes = QByteArray::fromHex("fffe");
            for (QChar c : text) {
                bytes.append(char(c.unicode() & 0xff));
                bytes.append(char(c.unicode() >> 8));
            }
        }
        QVERIFY(AtomicFileWriter::write(project.filePath(), bytes));
        const auto loaded = LabelPlusDocument::readFromFile(project.filePath());
        QCOMPARE(loaded.sha256, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        QCOMPARE(loaded.project.images()[0].labels[0].text(), QStringLiteral("original"));
        ProjectController controller;
        controller.loadFromFile(project.filePath(), *FileRevision::read(project.filePath()));
        QCOMPARE(controller.savedRevision().sha256, loaded.sha256);
    }

    void aliasesDoNotBypassSaveConflicts()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        const auto alias = dir.filePath("alias.txt");
        QVERIFY(QFile::link(project.filePath(), alias));
        QVERIFY(ProjectFileTransaction::sameTarget(alias, project.filePath()));
        edit(project, "external");
        QVERIFY(writeProject(project));
        const auto disk = FileRevision::read(project.filePath());
        edit(controller.project(), "local");
        controller.markDirty();
        QVERIFY_EXCEPTION_THROWN(controller.saveAs(alias), ProjectFileConflict);
        QVERIFY(controller.isDirty());
        QVERIFY(FileRevision::read(project.filePath())->sameContent(*disk));
#else
        QSKIP("Symbolic-link fixture requires Unix.");
#endif
    }

    void backupNamesAndIndexesAreNeverOverwritten()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        const auto path = dir.filePath("backup.txt");
        QVERIFY(ProjectRecoveryService::writeBackup(project, path, true));
        const auto original = FileRevision::read(path);
        const auto index = FileRevision::read(path + ".labelqt-backup.json");
        edit(project, "second instance");
        QVERIFY(!ProjectRecoveryService::writeBackup(project, path, true));
        QVERIFY(FileRevision::read(path)->sameContent(*original));
        QVERIFY(FileRevision::read(path + ".labelqt-backup.json")->sameContent(*index));
        const auto other = dir.filePath("orphan.txt");
        QVERIFY(AtomicFileWriter::write(other + ".labelqt-backup.json", "existing record"));
        const auto oldIndex = FileRevision::read(other + ".labelqt-backup.json");
        QVERIFY(!ProjectRecoveryService::writeBackup(project, other));
        QVERIFY(FileRevision::read(other + ".labelqt-backup.json")->sameContent(*oldIndex));
    }

    void revisionsRejectNonRegularFiles()
    {
        QTemporaryDir dir;
        QVERIFY(!FileRevision::read(dir.path()));
#ifdef Q_OS_UNIX
        const auto fifo = dir.filePath("pipe");
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        QVERIFY(!FileRevision::read(fifo));
        const auto link = dir.filePath("dangling.txt");
        QVERIFY(QFile::link(dir.filePath("missing.txt"), link));
        QVERIFY(!FileRevision::read(link));
#endif
        QVERIFY(FileRevision::read(dir.filePath("missing.txt")));
        QVERIFY(!FileRevision::read(dir.filePath("missing.txt"))->exists);
    }

    void observationCallbacksCanDestroyTheMonitor()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        QPointer<ProjectFileMonitor> monitor = new ProjectFileMonitor;
        monitor->setBaseline(project.filePath(), *FileRevision::read(project.filePath()));
        connect(monitor, &ProjectFileMonitor::observationChanged, this, [&] { delete monitor.data(); });
        monitor->checkNow();
        QTRY_VERIFY(!monitor);
        monitor = new ProjectFileMonitor;
        connect(monitor, &ProjectFileMonitor::observationChanged, this, [&] { delete monitor.data(); });
        monitor->setBaseline(project.filePath(), *FileRevision::read(project.filePath()));
        QVERIFY(!monitor);
    }

    void windowCanBeDestroyedBeforeTheCoordinator()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        auto* window = new QWidget;
        ProjectExternalChangeController changes(window, controller, {[] { return false; }, [](auto, auto) {}});
        delete window;
        QVERIFY(!changes.notice());
        // Storage events may still arrive while the owner is shutting down.
        controller.save();
        QTest::qWait(500);
    }

    void promptCallbackCanDestroyTheCoordinator()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        QWidget window;
        window.show();
        window.activateWindow();
        QPointer<ProjectExternalChangeController> changes;
        changes = new ProjectExternalChangeController(
            &window, controller, {[] { return false; }, [&](auto, auto) { delete changes.data(); }});
        edit(project, "external");
        QVERIFY(writeProject(project));
        QTRY_VERIFY(!changes);
    }

    void replacementInPlaceDeletionAndRecreation()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        const auto baseline = FileRevision::read(project.filePath());
        QVERIFY(baseline);
        ProjectFileMonitor monitor;
        monitor.setBaseline(project.filePath(), *baseline);
        monitor.checkNow();
        QTRY_VERIFY(monitor.observedRevision());
        QVERIFY(!monitor.hasExternalChange());
        edit(project, "replacement");
        QVERIFY(writeProject(project));
        QTRY_VERIFY(monitor.hasExternalChange());
        const auto replacement = FileRevision::read(project.filePath());
        QTRY_VERIFY(monitor.observedRevision()->sameContent(*replacement));
        // The file watch must be reattached after atomic replacement.
        edit(project, "in-place");
        QFile file(project.filePath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto bytes = LabelPlusDocument::serialize(project).toUtf8();
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const auto inPlace = FileRevision::read(project.filePath());
        QTRY_VERIFY(monitor.observedRevision()->sameContent(*inPlace));
        QVERIFY(QFile::remove(project.filePath()));
        QTRY_VERIFY(monitor.observedRevision() && !monitor.observedRevision()->exists);
        QVERIFY(writeProject(project));
        QTRY_VERIFY(monitor.observedRevision()->sameContent(*inPlace));
        QVERIFY(AtomicFileWriter::write(project.filePath(), "invalid document"));
        QTRY_VERIFY(!monitor.error().isEmpty());
        QVERIFY(writeProject(project));
        QTRY_VERIFY(monitor.error().isEmpty());
    }

    void ownWritesAndOldSessionResults()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("first.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        ProjectFileMonitor monitor;
        const auto sync = [&] { monitor.setBaseline(controller.project().filePath(), controller.savedRevision()); };
        connect(&controller, &ProjectController::storageChanged, &monitor, sync);
        sync();
        monitor.checkNow();
        edit(controller.project(), "own edit");
        controller.markDirty();
        controller.save();
        monitor.checkNow();
        QTRY_VERIFY(monitor.observedRevision());
        QVERIFY(!monitor.hasExternalChange());
        const auto revision = controller.savedRevision();
        QVERIFY(writeProject(controller.project()));
        monitor.checkNow();
        QTest::qWait(500);
        QVERIFY(!monitor.hasExternalChange());
        QCOMPARE(controller.savedRevision().sha256, revision.sha256);
        auto second = fixture(dir.filePath("second.txt"));
        QVERIFY(writeProject(second));
        monitor.checkNow();
        controller.loadFromFile(second.filePath());
        QTRY_VERIFY(monitor.observedRevision());
        QVERIFY(monitor.observedRevision()->sameContent(controller.savedRevision()));
        QVERIFY(!monitor.hasExternalChange());
        monitor.checkNow();
        monitor.setBaseline({}, {});
        QTest::qWait(500);
        QVERIFY(!monitor.observedRevision());
    }

    void ignoredChangeStillConflictsOnSave()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController first, second;
        first.loadFromFile(project.filePath());
        second.loadFromFile(project.filePath());
        ProjectFileMonitor monitor;
        monitor.setBaseline(first.project().filePath(), first.savedRevision());
        edit(second.project(), "external");
        second.markDirty();
        second.save();
        QTRY_VERIFY(monitor.hasExternalChange());
        edit(first.project(), "local");
        first.markDirty();
        QVERIFY_EXCEPTION_THROWN(first.save(), ProjectFileConflict);
        QCOMPARE(LabelPlusDocument::loadFromFile(project.filePath()).images()[0].labels[0].text(),
                 QStringLiteral("external"));
        QVERIFY(first.isDirty());
    }

    void reloadIsVersionCheckedAndBackupsPreserveBothVersions()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        const auto session = controller.revisions().session();
        edit(controller.project(), "local");
        controller.markDirty();
        edit(project, "external");
        QVERIFY(writeProject(project));
        const auto observed = FileRevision::read(project.filePath());
        QVERIFY(observed);
        const auto backup = ProjectRecoveryService::preserveLocalEdits(controller.project(), AppPreferences{});
        QVERIFY(backup);
        QCOMPARE(LabelPlusDocument::loadFromFile(*backup).images()[0].labels[0].text(), QStringLiteral("local"));
        QVERIFY(FileRevision::read(project.filePath())->sameContent(*observed));
        edit(project, "newer external");
        QVERIFY(writeProject(project));
        QVERIFY_EXCEPTION_THROWN(controller.loadFromFile(project.filePath(), *observed), ProjectFileConflict);
        QCOMPARE(controller.project().images()[0].labels[0].text(), QStringLiteral("local"));
        QCOMPARE(controller.revisions().session(), session);
        QVERIFY(controller.isDirty());
        QVERIFY(AtomicFileWriter::write(project.filePath(), "invalid document"));
        QVERIFY_EXCEPTION_THROWN(controller.loadFromFile(project.filePath()), std::exception);
        QVERIFY(controller.isDirty());
        QCOMPARE(controller.revisions().session(), session);
        QVERIFY(writeProject(project));
        controller.loadFromFile(project.filePath(), *FileRevision::read(project.filePath()));
        QVERIFY(!controller.isDirty());
        QVERIFY(controller.revisions().session() != session);
        QCOMPARE(controller.project().images()[0].labels[0].text(), QStringLiteral("newer external"));
    }

    void backupFailureDoesNotTouchTheProject()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        const auto before = FileRevision::read(project.filePath());
        QVERIFY(AtomicFileWriter::write(dir.filePath("bak"), "not a directory"));
        edit(project, "local");
        QVERIFY(!ProjectRecoveryService::preserveLocalEdits(project, AppPreferences{}));
        QVERIFY(FileRevision::read(project.filePath())->sameContent(*before));
    }

    void restoreSelectionByIdentity()
    {
        auto project = fixture("project.txt");
        project.images()[0].labels.append(Label("second", "group", {}));
        ProjectSessionState state;
        state.imageName = project.images()[0].name;
        state.imageId = project.images()[0].id;
        state.selectedLabelIds = {project.images()[0].labels[0].id()};
        state.selectedLabelId = state.selectedLabelIds.first();
        project.images()[0].labels.swapItemsAt(0, 1);
        project.images().prepend({"other.png", {}, {}});
        QCOMPARE(state.resolveImageIndex(project), 1);
        QCOMPARE(state.resolveLabelIndexes(project), QVector<int>{1});
        QCOMPARE(state.resolvePrimaryLabelIndex(project), 1);
        project.images()[1].labels.removeAt(1);
        QVERIFY(state.resolveLabelIndexes(project).isEmpty());
        QCOMPARE(state.resolvePrimaryLabelIndex(project), -1);
        project.images().removeAt(1);
        QVERIFY(state.resolveLabelIndexes(project).isEmpty());
    }

    void reloadDialogPreservesLocalEdits_data()
    {
        QTest::addColumn<bool>("backupFails");
        QTest::newRow("success") << false;
        QTest::newRow("backup-failure") << true;
    }

    void reloadDialogPreservesLocalEdits()
    {
        QFETCH(bool, backupFails);
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        edit(project, "external");
        QVERIFY(writeProject(project));
        const auto observed = FileRevision::read(project.filePath());
        QVERIFY(observed);
        if (backupFails)
            QVERIFY(AtomicFileWriter::write(dir.filePath("bak"), "not a directory"));
        QWidget window;
        const AppPreferences preferences;
        int commits = 0, reloads = 0;
        ProjectRecoveryController recovery(&window, controller,
                                           {[&]() -> const AppPreferences& { return preferences; },
                                            [] { return false; }, [] { return false; }, [] {}, [](bool) {},
                                            [] { return false; },
                                            [&](const QString& path, std::optional<FileRevision> revision) {
                                                ++reloads;
                                                controller.loadFromFile(path, revision);
                                                return true;
                                            },
                                            [&] {
                                                ++commits;
                                                edit(controller.project(), "local editor buffer");
                                                controller.markDirty();
                                            }});
        int stage = 0;
        QTimer answers;
        answers.setInterval(10);
        connect(&answers, &QTimer::timeout, &window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box)
                return;
            if (stage == 0) {
                for (auto* button : box->buttons()) {
                    if (button->text() == QStringLiteral("Reload disk version...")) {
                        ++stage;
                        button->click();
                        return;
                    }
                }
            }
            else if (stage == 1 && box->standardButtons().testFlag(QMessageBox::Yes)) {
                ++stage;
                box->button(QMessageBox::Yes)->click();
            }
            else if (stage == 2 && backupFails && box->standardButtons().testFlag(QMessageBox::Ok)) {
                ++stage;
                box->button(QMessageBox::Ok)->click();
            }
        });
        answers.start();
        QCOMPARE(recovery.resolveDiskChange(*observed), !backupFails);
        QCOMPARE(commits, 1);
        QCOMPARE(reloads, backupFails ? 0 : 1);
        QVERIFY(FileRevision::read(project.filePath())->sameContent(*observed));
        if (backupFails) {
            QVERIFY(controller.isDirty());
            QCOMPARE(controller.project().images()[0].labels[0].text(), QStringLiteral("local editor buffer"));
        }
        else {
            QVERIFY(!controller.isDirty());
            const auto entries = ProjectRecoveryService::entries(project.filePath(), preferences);
            QCOMPARE(entries.size(), 1);
            const auto local = ProjectRecoveryService::preview(entries.first());
            QVERIFY(local);
            QCOMPARE(local->images()[0].labels[0].text(), QStringLiteral("local editor buffer"));
        }
    }

    void deferredPromptAndAcknowledgement()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        ProjectController controller;
        controller.loadFromFile(project.filePath());
        QWidget window;
        window.show();
        window.activateWindow();
        bool blocked = true;
        int offers = 0;
        ProjectExternalChangeController changes(&window, controller,
                                                {[&] { return blocked; }, [&](auto, const QString&) { ++offers; }});
        edit(project, "external");
        QVERIFY(writeProject(project));
        QTRY_VERIFY(!changes.notice()->isHidden());
        changes.notice()->click();
        QCOMPARE(offers, 0);
        blocked = false;
        QDialog modal(&window);
        modal.setModal(true);
        modal.show();
        QTRY_VERIFY(QApplication::activeModalWidget());
        changes.notice()->click();
        QCOMPARE(offers, 0);
        modal.close();
        window.activateWindow();
        QTRY_COMPARE(offers, 1);
        QTest::qWait(1200);
        QCOMPARE(offers, 1);
        QVERIFY(!changes.notice()->isHidden());
        changes.notice()->click();
        QCOMPARE(offers, 2);
        edit(project, "newer");
        QVERIFY(writeProject(project));
        QTRY_COMPARE(offers, 3);
        controller.loadFromFile(project.filePath());
        QTRY_VERIFY(changes.notice()->isHidden());
    }

    void destroyDuringRead()
    {
        QTemporaryDir dir;
        auto project = fixture(dir.filePath("project.txt"));
        QVERIFY(writeProject(project));
        {
            ProjectFileMonitor monitor;
            monitor.setBaseline(project.filePath(), *FileRevision::read(project.filePath()));
            monitor.checkNow();
        }
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
};
QTEST_MAIN(ExternalProjectChangeTests)
#include "ExternalProjectChangeTests.moc"
