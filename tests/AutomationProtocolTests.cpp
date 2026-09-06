#include "core/Project.h"
#include "core/UndoStack.h"
#include "services/automation/AutomationProtocol.h"
#include "services/automation/AutomationService.h"
#include "services/label/LabelEditController.h"
#include "services/project/ProjectController.h"
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QJsonObject envelope(QJsonArray changes = {})
{
    return {{"apiVersion", 2}, {"taskId", uuid()}, {"sessionId", uuid()}, {"changes", changes}};
}
} // namespace

class AutomationProtocolTests : public QObject {
    Q_OBJECT
private slots:
    void rejectsLegacyAndMalformedOutput()
    {
        QVERIFY(!AutomationProtocol::result({{"apiVersion", 1}, {"operations", QJsonArray{}}}));
        auto valid = envelope();
        QVERIFY(AutomationProtocol::result(valid));
        for (const auto& key : {"apiVersion", "taskId", "sessionId", "changes"}) {
            auto invalid = valid;
            invalid.remove(key);
            QVERIFY(!AutomationProtocol::result(invalid));
        }
        valid.insert("operations", QJsonArray{});
        QVERIFY(!AutomationProtocol::result(valid));
        QJsonObject change{{"pageId", uuid()}, {"labelId", uuid()}, {"text", ""}};
        QVERIFY(AutomationProtocol::result(envelope({change})));
        QVERIFY(!AutomationProtocol::result(envelope({change, change})));
        change.insert("text", QJsonValue::Null);
        QVERIFY(!AutomationProtocol::result(envelope({change})));
        change.remove("text");
        change.insert("position", QJsonObject{{"x", 1.1}, {"y", 0.5}});
        QVERIFY(!AutomationProtocol::result(envelope({change})));
        change.insert("position", QJsonObject{{"x", 0.1}, {"y", "0.5"}});
        QVERIFY(!AutomationProtocol::result(envelope({change})));
        change.remove("position");
        change.insert("groupId", "");
        QVERIFY(!AutomationProtocol::result(envelope({change})));
    }

    void manifestScopesAreExplicit()
    {
        QJsonObject spec{{"scope", "selectedLabels"}, {"read", QJsonArray{"text"}}, {"write", QJsonArray{"text"}}};
        QVERIFY(AutomationProtocol::parseTaskSpec(spec));
        spec.insert("allowCreate", true);
        QVERIFY(!AutomationProtocol::parseTaskSpec(spec));
        spec.insert("scope", "currentPage");
        QVERIFY(AutomationProtocol::parseTaskSpec(spec));
        spec.insert("write", QJsonArray{"imageSource"});
        QVERIFY(!AutomationProtocol::parseTaskSpec(spec));
        spec.insert("write", QJsonArray{});
        spec.insert("read", QJsonArray{"txt"});
        QVERIFY(!AutomationProtocol::parseTaskSpec(spec));
    }

    void selectedScopeDoesNotFallBackToWholePage()
    {
        Project project;
        project.setGroups({"dialog"});
        project.images() = {{"a.png", {}, {Label("a", "dialog", {0.2, 0.3}), Label("b", "dialog", {0.4, 0.5})}}};
        AutomationTaskSpec spec;
        spec.readFields = {ProjectField::Text};
        spec.writeFields = {ProjectField::Text};
        QVERIFY(!AutomationProtocol::plan(spec, project, 0, {}));
        const auto request = AutomationProtocol::plan(spec, project, 0, {1});
        QVERIFY(request);
        QVERIFY(request->pages.isEmpty());
        QCOMPARE(request->writeLabels, QSet<QUuid>{project.images()[0].labels[1].id()});
    }

    void nativeCreateEditDeleteShareOneUndo()
    {
        ProjectController controller;
        Project project;
        project.setGroups({"dialog"});
        project.images() = {{"a.png", {}, {Label("a", "dialog", {0.1, 0.2}), Label("b", "dialog", {0.3, 0.4})}}};
        controller.setProject(std::move(project));
        UndoStack undo;
        LabelEditController editor(controller, undo, {});
        ProjectTaskService tasks(controller, editor);
        AutomationTaskSpec spec;
        spec.scope = AutomationScope::CurrentPage;
        spec.writeFields = {ProjectField::Text, ProjectField::Position, ProjectField::Deleted};
        spec.allowCreate = true;
        const auto request = AutomationProtocol::plan(spec, controller.project(), 0, {});
        QVERIFY(request);
        const auto snapshot = tasks.capture(*request);
        QVERIFY(snapshot);
        const auto pageId = snapshot->pages.first().toString(QUuid::WithoutBraces);
        const auto firstId = snapshot->labels[0].id.toString(QUuid::WithoutBraces);
        const auto secondId = snapshot->labels[1].id.toString(QUuid::WithoutBraces);
        const QJsonArray changes{
            QJsonObject{{"pageId", pageId},
                        {"labelId", firstId},
                        {"text", "new"},
                        {"position", QJsonObject{{"x", 0.8}, {"y", 0.9}}}},
            QJsonObject{{"pageId", pageId}, {"labelId", secondId}, {"deleted", true}},
            QJsonObject{{"pageId", pageId},
                        {"text", "added"},
                        {"groupId", snapshot->groups.cbegin().key().toString(QUuid::WithoutBraces)},
                        {"position", QJsonObject{{"x", 0.5}, {"y", 0.5}}}}};
        const auto decoded = AutomationProtocol::result(envelope(changes));
        QVERIFY(decoded);
        const auto receipt = tasks.submit(snapshot->taskId, decoded->changes, "Batch");
        QVERIFY(receipt);
        QCOMPARE(receipt->labels.createdLabels.size(), 1);
        const auto createdId = receipt->labels.createdLabels.first();
        const auto& labels = std::as_const(controller.project()).images()[0].labels;
        QCOMPARE(labels.size(), 3);
        QCOMPARE(labels[0].text(), QString("new"));
        QVERIFY(labels[1].isDeleted());
        undo.undo();
        QCOMPARE(labels.size(), 2);
        QCOMPARE(labels[0].text(), QString("a"));
        QCOMPARE(labels[0].position(), QPointF(0.1, 0.2));
        QVERIFY(!labels[1].isDeleted());
        QVERIFY(!undo.canUndo());
        undo.redo();
        QCOMPARE(labels.last().id(), createdId);
    }

    void pythonSdkRunsThroughNativeCommit()
    {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            QSKIP("Python 3 is unavailable");
        ProjectController controller;
        Project project;
        project.setGroups({"dialog"});
        project.images() = {{"a.png", {}, {Label("original", "dialog", {0.2, 0.3})}}};
        controller.setProject(std::move(project));
        UndoStack undo;
        LabelEditController editor(controller, undo, {});
        ProjectTaskService tasks(controller, editor);
        AutomationTaskSpec spec;
        spec.readFields = {ProjectField::Text};
        spec.writeFields = {ProjectField::Text};
        const auto request = AutomationProtocol::plan(spec, controller.project(), 0, {0});
        QVERIFY(request);
        const auto snapshot = tasks.capture(*request);
        QVERIFY(snapshot);
        const auto input = AutomationProtocol::input(*snapshot, {});
        QVERIFY(!input.contains("project"));
        QVERIFY(!input.value("pages").toArray().first().toObject().contains("imageSource"));
        QVERIFY(!input.value("labels").toArray().first().toObject().contains("labelIndex"));
        AutomationScript script;
        script.directoryPath = QStringLiteral(LABELQT_SOURCE_DIR "/scripts/official/test");
        script.entryPath = script.directoryPath + "/prefix_selected.py";
        AutomationRunner runner;
        AutomationRunResult result;
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        connect(&runner, &AutomationRunner::finished, this, [&](const AutomationRunResult& value) { result = value; });
        AutomationPythonSettings settings;
        settings.command = python;
        runner.start(script, *snapshot, {{"prefix", "Reviewed: "}}, {}, {}, settings);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY2(result.success, qPrintable(result.error + result.standardError));
        QCOMPARE(result.output.taskId, snapshot->taskId);
        QVERIFY(tasks.submit(result.output.taskId, result.output.changes, "Script"));
        QCOMPARE(controller.project().images()[0].labels[0].text(), QString("Reviewed: original"));
        undo.undo();
        QCOMPARE(controller.project().images()[0].labels[0].text(), QString("original"));
        undo.redo();
        QCOMPARE(controller.project().images()[0].labels[0].text(), QString("Reviewed: original"));
    }

    void runnerRejectsResultForDifferentTask()
    {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            QSKIP("Python 3 is unavailable");
        QTemporaryDir dir;
        QFile file(dir.filePath("wrong.py"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("import argparse,json,uuid\np=argparse.ArgumentParser()\np.add_argument('--input')\n"
                   "p.add_argument('--output')\na=p.parse_args()\n"
                   "json.dump({'apiVersion':2,'taskId':str(uuid.uuid4()),'sessionId':str(uuid.uuid4()),"
                   "'changes':[]},open(a.output,'w'))\n");
        file.close();
        AutomationScript script;
        script.directoryPath = dir.path();
        script.entryPath = file.fileName();
        AutomationRunner runner;
        AutomationRunResult result;
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        connect(&runner, &AutomationRunner::finished, this, [&](const AutomationRunResult& value) { result = value; });
        AutomationPythonSettings settings;
        settings.command = python;
        ProjectTaskSnapshot snapshot;
        snapshot.taskId = QUuid::createUuid();
        snapshot.sessionId = QUuid::createUuid();
        runner.start(script, snapshot, {}, {}, {}, settings);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!result.success);
        QVERIFY(result.output.changes.isEmpty());
    }

    void completionCanStartAnotherTaskOnSameRunner()
    {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            QSKIP("Python 3 is unavailable");
        QTemporaryDir dir;
        QFile file(dir.filePath("echo.py"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("import argparse,json\np=argparse.ArgumentParser()\np.add_argument('--input')\n"
                   "p.add_argument('--output')\na=p.parse_args()\nv=json.load(open(a.input))\n"
                   "json.dump({'apiVersion':2,'taskId':v['taskId'],'sessionId':v['sessionId'],"
                   "'changes':[]},open(a.output,'w'))\n");
        file.close();
        AutomationScript script;
        script.directoryPath = dir.path();
        script.entryPath = file.fileName();
        AutomationPythonSettings settings;
        settings.command = python;
        ProjectTaskSnapshot snapshot;
        snapshot.taskId = QUuid::createUuid();
        snapshot.sessionId = QUuid::createUuid();
        AutomationRunner runner;
        int count = 0;
        bool success = true;
        connect(&runner, &AutomationRunner::finished, this, [&](const AutomationRunResult& result) {
            success = success && result.success && result.output.taskId == snapshot.taskId;
            ++count;
            if (count == 1) {
                snapshot.taskId = QUuid::createUuid();
                runner.start(script, snapshot, {}, {}, {}, settings);
            }
        });
        runner.start(script, snapshot, {}, {}, {}, settings);
        QTRY_COMPARE_WITH_TIMEOUT(count, 2, 10000);
        QVERIFY(success);
        QVERIFY(!runner.isRunning());
    }

    void cancellationRejectsAlreadyWrittenResult()
    {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            QSKIP("Python 3 is unavailable");
        QTemporaryDir dir;
        QFile file(dir.filePath("wait.py"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("import argparse,json,time\np=argparse.ArgumentParser()\np.add_argument('--input')\n"
                   "p.add_argument('--output')\na=p.parse_args()\nv=json.load(open(a.input))\n"
                   "with open(a.output,'w') as f:\n"
                   " json.dump({'apiVersion':2,'taskId':v['taskId'],'sessionId':v['sessionId'],'changes':[]},f)\n"
                   "print('result-written',flush=True)\ntime.sleep(30)\n");
        file.close();
        AutomationScript script;
        script.directoryPath = dir.path();
        script.entryPath = file.fileName();
        AutomationPythonSettings settings;
        settings.command = python;
        ProjectTaskSnapshot snapshot;
        snapshot.taskId = QUuid::createUuid();
        snapshot.sessionId = QUuid::createUuid();
        AutomationRunner runner;
        AutomationRunResult result;
        QSignalSpy finished(&runner, &AutomationRunner::finished);
        connect(&runner, &AutomationRunner::finished, this, [&](const AutomationRunResult& value) { result = value; });
        QString log;
        connect(&runner, &AutomationRunner::standardOutputReceived, this, [&](const QString& value) {
            log += value;
            if (log.contains("result-written"))
                runner.cancel();
        });
        runner.start(script, snapshot, {}, {}, {}, settings);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!result.success);
        QVERIFY(result.output.changes.isEmpty());
        QVERIFY(result.error.contains("canceled"));
        QVERIFY(!runner.isRunning());
    }
};
QTEST_GUILESS_MAIN(AutomationProtocolTests)
#include "AutomationProtocolTests.moc"
