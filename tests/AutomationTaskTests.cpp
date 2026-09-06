#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "core/ProjectIdentityService.h"
#include "core/ProjectMetadataService.h"
#include "core/UndoStack.h"
#include "services/automation/AutomationProtocol.h"
#include "services/automation/ProjectTaskService.h"
#include "services/label/LabelEditController.h"
#include "services/project/ProjectController.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
#include <thread>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
struct Host {
    ProjectController controller;
    UndoStack undo;
    LabelEditController editor{controller, undo, {}};
    ProjectTaskService tasks{controller, editor};

    Host()
    {
        Project project;
        project.setGroups({"dialog", "background"});
        project.images() = {{"a.png", {}, {Label("a", "dialog", {0.2, 0.3})}},
                            {"b.png", {}, {Label("b", "dialog", {0.4, 0.5})}}};
        controller.setProject(std::move(project));
    }
    Project& project()
    {
        return controller.project();
    }
    ProjectTaskRequest request(int page = 0)
    {
        ProjectTaskRequest request;
        request.pages.insert(project().images().at(page).id);
        request.writePages = request.pages;
        request.writeFields = {ProjectField::Text};
        return request;
    }
    LabelChange change(int page = 0)
    {
        const auto& image = project().images().at(page);
        return {image.id, image.labels.first().id(), QStringLiteral("result"), {}, {}, {}};
    }
};
} // namespace

class AutomationTaskTests : public QObject {
    Q_OBJECT
private slots:
    void reloadInvalidatesOutstandingResults()
    {
        QTemporaryDir directory;
        Host host;
        const auto path = directory.filePath("project.txt");
        host.project().setFilePath(path);
        QVERIFY(AtomicFileWriter::write(path, LabelPlusDocument::serialize(host.project()).toUtf8()));
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        const auto change = host.change();
        host.controller.loadFromFile(path);
        QVERIFY(!host.tasks.submit(task->taskId, {change}, "old session"));
        QCOMPARE(host.project().images()[0].labels[0].text(), QStringLiteral("a"));
        QVERIFY(!host.undo.canUndo());
    }

    void emptyTextAndLineEndingsKeepIds()
    {
        for (const QString& text : {QString(), QStringLiteral(""), QStringLiteral("\n"), QStringLiteral("\r\n"),
                                    QStringLiteral("text\rline\n")}) {
            Host host;
            host.project().images()[0].labels[0].setText(text);
            const auto loaded =
                LabelPlusDocument::parse(LabelPlusDocument::serialize(host.project()), "/tmp/project.txt");
            QCOMPARE(loaded.images()[0].labels[0].id(), host.project().images()[0].labels[0].id());
            QCOMPARE(loaded.images()[0].labels[0].text(), text);
        }
    }

    void versionSnapshotsDoNotRelocateLivePageContainers()
    {
        Host host;
        const auto* labels = &std::as_const(host.project()).images()[0].labels;
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        host.editor.setLabelText(0, 0, "edited");
        QCOMPARE(&std::as_const(host.project()).images()[0].labels, labels);
        host.undo.undo();
        QCOMPARE(&std::as_const(host.project()).images()[0].labels, labels);
        QCOMPARE(labels->first().text(), QString("a"));
    }

    void contextReadDoesNotAuthorizeContextWrites()
    {
        Host host;
        auto request = host.request();
        request.pages.insert(host.project().images()[1].id);
        auto task = host.tasks.capture(request);
        QVERIFY(task);
        QCOMPARE(host.tasks.submit(task->taskId, {host.change(1)}, "wrong target").error(), TaskError::NotAllowed);
        task = host.tasks.capture(request);
        host.editor.setLabelText(1, 0, "context changed");
        QCOMPARE(host.tasks.submit(task->taskId, {host.change()}, "stale context").error(), TaskError::Conflict);
    }

    void sourceContentChangesConflictEvenWithoutRename()
    {
        Host host;
        QTemporaryDir dir;
        const auto path = dir.filePath("a.png");
        QVERIFY(AtomicFileWriter::write(path, "original"));
        host.project().images()[0].path = path;
        host.controller.markDirty();
        auto request = host.request();
        request.readFields.insert(ProjectField::ImageSource);
        const auto task = host.tasks.capture(request);
        QVERIFY(task);
        QVERIFY(AtomicFileWriter::write(path, "replaced"));
        QCOMPARE(host.tasks.submit(task->taskId, {host.change()}, "ocr").error(), TaskError::Conflict);
        QVERIFY(!host.undo.canUndo());
    }

    void simultaneousTasksOnlyConflictOnDependencies()
    {
        Host host;
        const auto first = host.tasks.capture(host.request());
        const auto same = host.tasks.capture(host.request());
        const auto other = host.tasks.capture(host.request(1));
        QVERIFY(first && same && other);
        QVERIFY(host.tasks.submit(first->taskId, {host.change()}, "first"));
        QCOMPARE(host.tasks.submit(same->taskId, {host.change()}, "same").error(), TaskError::Conflict);
        QVERIFY(host.tasks.submit(other->taskId, {host.change(1)}, "other"));
    }

    void identityRoundTripAndForeignEdit()
    {
        Host host;
        auto& project = host.project();
        project.images()[0].labels[0].setText("a\n\n");
        auto loaded = LabelPlusDocument::parse(LabelPlusDocument::serialize(project), "/tmp/project.txt");
        QCOMPARE(loaded.id(), project.id());
        QCOMPARE(loaded.groupIds(), project.groupIds());
        QCOMPARE(loaded.images()[0].id, project.images()[0].id);
        QCOMPARE(loaded.images()[0].labels[0].id(), project.images()[0].labels[0].id());
        loaded.images()[0].name = "renamed.png";
        loaded.images().swapItemsAt(0, 1);
        const auto savedAgain = LabelPlusDocument::parse(LabelPlusDocument::serialize(loaded), "/tmp/project.txt");
        QCOMPARE(savedAgain.images()[1].id, project.images()[0].id);
        auto foreign = LabelPlusDocument::serialize(project);
        foreign.replace("\na\n", "\nforeign\n");
        const auto changed = LabelPlusDocument::parse(foreign, "/tmp/project.txt");
        QVERIFY(changed.images()[0].labels[0].id() != project.images()[0].labels[0].id());
    }

    void identityValidationAndDeletedRows()
    {
        Host host;
        auto project = host.project();
        project.images()[0].labels.append(project.images()[0].labels[0]);
        QVERIFY(!ProjectIdentityService::valid(project));
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::serialize(project), std::runtime_error);
        project.images()[0].labels[1] = project.images()[0].labels[0].duplicate();
        const auto retained = project.images()[0].labels[1].id();
        project.images()[0].labels[0].setDeleted(true);
        const auto loaded = LabelPlusDocument::parse(LabelPlusDocument::serialize(project), "/tmp/project.txt");
        QCOMPARE(loaded.images()[0].labels.size(), 1);
        QCOMPARE(loaded.images()[0].labels[0].id(), retained);
        auto section = ProjectIdentityService::encode(project);
        section.insert("project", "invalid");
        QVERIFY(!ProjectIdentityService::restore(project, section));
        QVERIFY(ProjectIdentityService::valid(project));
    }

    void pasteAndGroupUndoPreserveIdentityRules()
    {
        Host host;
        const auto original = host.project().images()[0].labels[0].id();
        host.editor.pasteLabels(0, host.project().images()[0].labels, 0);
        const auto pasted = host.project().images()[0].labels[1].id();
        QVERIFY(pasted != original);
        host.undo.undo();
        host.undo.redo();
        QCOMPARE(host.project().images()[0].labels[1].id(), pasted);
        const auto group = host.project().groupId("dialog");
        host.editor.removeGroup("dialog", "background");
        host.undo.undo();
        QCOMPARE(host.project().groupId("dialog"), group);
        host.undo.redo();
        host.editor.addGroup("dialog");
        QVERIFY(host.project().groupId("dialog") != group);
    }

    void unrelatedPageAndFieldEditsRemainConcurrent()
    {
        Host host;
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        QCOMPARE(task->labels.size(), 1);
        QVERIFY(!task->labels[0].position);
        host.editor.setLabelText(1, 0, "user text");
        host.editor.setLabelPosition(0, 0, {0.8, 0.9});
        const auto committed = host.tasks.submit(task->taskId, {host.change()}, "task");
        QVERIFY(committed);
        QCOMPARE(committed->labels.changedLabels.size(), 1);
        QCOMPARE(host.project().images()[0].labels[0].text(), QString("result"));
        host.undo.undo();
        QCOMPARE(host.project().images()[0].labels[0].text(), QString("a"));
        QCOMPARE(host.project().images()[0].labels[0].position(), QPointF(0.8, 0.9));
        QCOMPARE(host.project().images()[1].labels[0].text(), QString("user text"));
        host.undo.redo();
        QVERIFY(!host.tasks.submit(task->taskId, {host.change()}, "duplicate"));
    }

    void undoDoesNotReviveOldDependency()
    {
        Host host;
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        host.editor.setLabelText(0, 0, "user edit");
        host.undo.undo();
        QCOMPARE(host.project().images()[0].labels[0].text(), QString("a"));
        const auto result = host.tasks.submit(task->taskId, {host.change()}, "task");
        QVERIFY(!result);
        QCOMPARE(result.error(), TaskError::Conflict);
        QVERIFY(host.undo.canRedo());
    }

    void pageOrderAndSourceAreExplicitDependencies()
    {
        Host host;
        auto request = host.request();
        const auto independent = host.tasks.capture(request);
        request.readPageOrder = true;
        const auto ordered = host.tasks.capture(request);
        request.readPageOrder = false;
        request.readFields.insert(ProjectField::ImageSource);
        const auto source = host.tasks.capture(request);
        QVERIFY(independent && ordered && source);
        const auto change = host.change();
        host.project().images()[0].name = "cover.png";
        host.project().images().swapItemsAt(0, 1);
        host.controller.markDirty();
        QCOMPARE(host.tasks.submit(ordered->taskId, {change}, "order").error(), TaskError::Conflict);
        QCOMPARE(host.tasks.submit(source->taskId, {change}, "source").error(), TaskError::Conflict);
        QVERIFY(host.tasks.submit(independent->taskId, {change}, "independent"));
        QCOMPARE(host.project().images()[1].labels[0].text(), QString("result"));
    }

    void labelScopeSurvivesUnrelatedInsertion()
    {
        Host host;
        auto request = host.request();
        request.pages.clear();
        request.writePages.clear();
        request.labels.insert(host.change().labelId);
        request.writeLabels = request.labels;
        const auto task = host.tasks.capture(request);
        QVERIFY(task);
        host.editor.pasteLabels(0, {Label("new", "dialog", {})}, -1);
        QVERIFY(host.tasks.submit(task->taskId, {host.change()}, "task"));
    }

    void deletionAndUndoConflict()
    {
        Host host;
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        host.editor.deleteLabels(0, {0});
        host.undo.undo();
        QCOMPARE(host.tasks.submit(task->taskId, {host.change()}, "task").error(), TaskError::Conflict);
    }

    void failedBatchIsAtomicAndCapabilityLimited()
    {
        Host host;
        auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        auto valid = host.change();
        auto wrong = host.change(1);
        QCOMPARE(host.tasks.submit(task->taskId, {valid, wrong}, "task").error(), TaskError::NotAllowed);
        QVERIFY(!host.undo.canUndo());
        QCOMPARE(host.project().images()[0].labels[0].text(), QString("a"));
        auto request = host.request();
        request.writeFields.insert(ProjectField::Position);
        task = host.tasks.capture(request);
        wrong = valid;
        wrong.position = QPointF(std::numeric_limits<double>::quiet_NaN(), 0);
        QCOMPARE(host.tasks.submit(task->taskId, {wrong}, "task").error(), TaskError::InvalidChange);
        QVERIFY(!host.undo.canUndo());
        request.writeFields.clear();
        task = host.tasks.capture(request);
        QCOMPARE(host.tasks.submit(task->taskId, {valid}, "task").error(), TaskError::NotAllowed);
    }

    void additionsUseOneUndoAndKeepIds()
    {
        Host host;
        auto request = host.request();
        request.allowCreate = true;
        const auto task = host.tasks.capture(request);
        QVERIFY(task);
        LabelChange addition{host.change().pageId, {}, QStringLiteral("ocr"), host.project().groupId("dialog"),
                             QPointF(0.1, 0.2),    {}};
        const auto receipt = host.tasks.submit(task->taskId, {host.change(), addition}, "task");
        QVERIFY(receipt);
        const auto id = host.project().images()[0].labels[1].id();
        QCOMPARE(receipt->labels.createdLabels, QVector<QUuid>{id});
        QCOMPARE(receipt->taskId, task->taskId);
        QVERIFY(receipt->revision > task->revision);
        host.undo.undo();
        QCOMPARE(host.project().images()[0].labels.size(), 1);
        QCOMPARE(host.project().images()[0].labels[0].text(), QString("a"));
        QVERIFY(!host.undo.canUndo());
        host.undo.redo();
        QCOMPARE(host.project().images()[0].labels[1].id(), id);
    }

    void reloadAndSaveAsInvalidateSessions()
    {
        Host host;
        const auto task = host.tasks.capture(host.request());
        QVERIFY(task);
        host.controller.setProject(host.project());
        QCOMPARE(host.tasks.submit(task->taskId, {host.change()}, "task").error(), TaskError::StaleSession);
        const auto second = host.tasks.capture(host.request());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto identity = host.project().id();
        host.controller.saveAs(dir.filePath("saved.txt"));
        QCOMPARE(host.project().id(), identity);
        QCOMPARE(host.tasks.submit(second->taskId, {host.change()}, "task").error(), TaskError::StaleSession);
    }

    void cancellationAndThreadAffinity()
    {
        Host host;
        const auto request = host.request();
        const auto task = host.tasks.capture(request);
        QVERIFY(task);
        std::expected<ProjectTaskSnapshot, TaskError> wrongThread;
        std::thread worker([&] { wrongThread = host.tasks.capture(request); });
        worker.join();
        QVERIFY(!wrongThread);
        QCOMPARE(wrongThread.error(), TaskError::WrongThread);
        bool delivered = false;
        const auto change = host.change();
        std::thread sender([&] {
            QMetaObject::invokeMethod(
                &host.tasks,
                [&, id = task->taskId, change] {
                    const auto result = host.tasks.submit(id, {change}, "late");
                    QVERIFY(!result);
                    QCOMPARE(result.error(), TaskError::UnknownTask);
                    delivered = true;
                },
                Qt::QueuedConnection);
        });
        sender.join();
        QVERIFY(host.tasks.cancel(task->taskId));
        QTRY_VERIFY(delivered);
        QVERIFY(!host.undo.canUndo());
    }

    void missingOutputTextCannotSilentlyEraseText()
    {
        const QJsonObject operation{{"type", "setLabelText"}, {"page", "a.png"}, {"labelIndex", 0}};
        QVERIFY(!AutomationProtocol::result({{"operations", QJsonArray{operation}}}));
    }
};
QTEST_GUILESS_MAIN(AutomationTaskTests)
#include "AutomationTaskTests.moc"
