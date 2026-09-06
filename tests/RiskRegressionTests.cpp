#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "core/ProjectMetadataService.h"
#include "services/io/ImagePageCache.h"
#include "services/io/ProjectFileTransaction.h"
#include "services/project/ProjectController.h"
#include "services/project/ProjectRecoveryService.h"
#include "ui/automation/AutomationController.h"
#include "ui/labeltable/LabelTableModel.h"
#include "ui/widgets/BackgroundTask.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPromise>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QtEndian>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
Project sample(const QString& path)
{
    Project project;
    project.setFilePath(path);
    project.setGroups({"group"});
    project.images().append({"page.png", {}, {Label("before", "group", {})}});
    return project;
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
QStringList metadata(const QByteArray& compressed)
{
    return {"# LabelQtMetadata v1", "# compression=qCompress", "# encoding=base64",
            "# payload=" + QString::fromLatin1(compressed.toBase64()), "# EndLabelQtMetadata"};
}
ImagePageLoadResult imageResult(const QString& path)
{
    ImagePageLoadResult result;
    result.path = path;
    result.image = QImage(2, 2, QImage::Format_RGB32);
    result.image.fill(Qt::green);
    result.sourceRevision = FileRevision::read(path).value();
    return result;
}
} // namespace

class RiskRegressionTests final : public QObject {
    Q_OBJECT
private slots:
    void modelSurvivesSnapshotDetachAndRelease()
    {
        auto project = sample({});
        LabelTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        model.setLabelsProvider([&project] { return &std::as_const(project).images().at(0).labels; });
        model.setGroupFilter({"group"});
        {
            const auto snapshot = project;
            project.images()[0].labels[0].setText("after");
            QCOMPARE(snapshot.images()[0].labels[0].text(), "before");
        }
        QCOMPARE(model.data(model.index(0, LabelTableModel::TextColumn)).toString(), "after");
        model.refresh();
        QCOMPARE(model.rowForSourceIndex(0), 0);
        model.setLabelsProvider({});
        project.clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void duplicatePageNamesCannotBeLoadedOrSaved()
    {
        QTemporaryDir dir;
        auto project = sample(dir.filePath("project.txt"));
        LabelPlusDocument::saveToFile(project, project.filePath());
        const auto original = bytes(project.filePath());
        project.images().append({"page.png", {}, {}});
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::saveToFile(project, project.filePath()), std::runtime_error);
        QCOMPARE(bytes(project.filePath()), original);
        const QString legacy = "1,0\n-\ngroup\n-\n\n>>>>>>>>[page.png]<<<<<<<<\n\n>>>>>>>>[page.png]<<<<<<<<\n";
        QCOMPARE(LabelPlusDocument::parse("1,0\n-\ngroup\n-\n>>>>>>>>[page.png]<<<<<<<<\n", project.filePath())
                     .images()
                     .size(),
                 1);
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::parse(legacy, project.filePath()), std::runtime_error);
    }

    void compressedMetadataLimitsActualOutput()
    {
        QVERIFY(ProjectMetadataService::tryMetadataObject(metadata(qCompress("{}"))));
        auto forged = qCompress("{}");
        qToBigEndian<quint32>(ProjectMetadataService::maxDecodedBytes + 1, forged.data());
        QVERIFY(!ProjectMetadataService::tryMetadataObject(metadata(forged)));
        auto oversized = qCompress(QByteArray(ProjectMetadataService::maxDecodedBytes + 1, ' '));
        qToBigEndian<quint32>(2, oversized.data());
        QVERIFY(!ProjectMetadataService::tryMetadataObject(metadata(oversized)));
        auto truncated = qCompress("{}");
        truncated.chop(1);
        QVERIFY(!ProjectMetadataService::tryMetadataObject(metadata(truncated)));
        QVERIFY(!ProjectMetadataService::tryMetadataObject(metadata(qCompress("{}") + "trailing")));
    }

    void projectWriteLockAndPreviousVersion()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        auto project = sample(path);
        LabelPlusDocument::saveToFile(project, path);
        ProjectController first, second;
        first.loadFromFile(path);
        second.loadFromFile(path);
        const auto original = bytes(path);
        {
            ProjectFileTransaction lock(path);
            QVERIFY_EXCEPTION_THROWN(second.save(), std::runtime_error);
            QVERIFY_EXCEPTION_THROWN(ProjectFileTransaction(dir.filePath("./project.txt")), std::runtime_error);
        }
        first.project().images()[0].labels[0].setText("changed");
        first.markDirty();
        first.save();
        QCOMPARE(bytes(ProjectFileTransaction::previousVersionPath(path)), original);
        QVERIFY_EXCEPTION_THROWN(second.save(), ProjectFileConflict);
        QCOMPARE(LabelPlusDocument::loadFromFile(path).images()[0].labels[0].text(), "changed");
        const auto recoveries = ProjectRecoveryService::entries(path, AppPreferences{});
        QVERIFY(!recoveries.isEmpty());
        const auto previous = ProjectRecoveryService::preview(recoveries.first());
        QVERIFY(previous);
        QCOMPARE(previous->images()[0].labels[0].text(), "before");
    }

    void externalWriteDuringTransactionIsRejected()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        QVERIFY(AtomicFileWriter::write(path, "original"));
        const auto original = FileRevision::read(path).value();
        ProjectFileTransaction transaction(path);
        QVERIFY(AtomicFileWriter::write(path, "external"));
        QVERIFY_EXCEPTION_THROWN(transaction.write("ours", original), ProjectFileConflict);
        QCOMPARE(bytes(path), "external");
    }

    void previousVersionFailurePreservesProjectAndDirtyState()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        LabelPlusDocument::saveToFile(sample(path), path);
        const auto original = bytes(path);
        ProjectController controller;
        controller.loadFromFile(path);
        controller.project().images()[0].labels[0].setText("changed");
        controller.markDirty();
        QVERIFY(QDir().mkdir(ProjectFileTransaction::previousVersionPath(path)));
        QVERIFY_EXCEPTION_THROWN(controller.save(), std::runtime_error);
        QCOMPARE(bytes(path), original);
        QVERIFY(controller.isDirty());
    }

    void destroyingCacheWhileWorkerRunsIsSafe()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("image");
        QVERIFY(AtomicFileWriter::write(path, "old"));
        QSemaphore entered, proceed;
        auto cache = std::make_unique<ImagePageCache>([&](const QString& name, QSize) {
            auto result = imageResult(name);
            entered.release();
            proceed.acquire();
            return result;
        });
        cache->requestImage(path, {});
        const auto cleanup = qScopeGuard([&] {
            proceed.release(4);
            QThreadPool::globalInstance()->waitForDone();
        });
        QVERIFY(entered.tryAcquire(1, 5000));
        cache.reset();
        proceed.release();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
    }

    void staleLoadCannotRepopulateCache()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("image");
        QVERIFY(AtomicFileWriter::write(path, "old"));
        QSemaphore entered, proceed;
        ImagePageCache cache([&](const QString& name, QSize) {
            auto result = imageResult(name);
            entered.release();
            proceed.acquire();
            return result;
        });
        QSignalSpy loaded(&cache, &ImagePageCache::imageLoaded);
        const auto cleanup = qScopeGuard([&] {
            proceed.release(4);
            QThreadPool::globalInstance()->waitForDone();
        });
        cache.requestImage(path, {});
        QVERIFY(entered.tryAcquire(1, 5000));
        QVERIFY(AtomicFileWriter::write(path, "new"));
        proceed.release();
        QTRY_VERIFY(entered.available() > 0);
        entered.acquire();
        QCOMPARE(loaded.size(), 0);
        proceed.release();
        QTRY_COMPARE(loaded.size(), 1);
        QVERIFY(qvariant_cast<ImagePageLoadResult>(loaded.first()[1]).isValid());
        QCOMPARE(qvariant_cast<ImagePageLoadResult>(loaded.first()[1]).sourceRevision.sha256,
                 FileRevision::read(path)->sha256);
        QVERIFY(cache.cachedImage(path, {}));

        cache.clear();
        cache.requestImage(path, {});
        QVERIFY(entered.tryAcquire(1, 5000));
        cache.clear();
        const auto newest = cache.requestImage(path, {});
        proceed.release(2);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QTRY_COMPARE(loaded.size(), 2);
        QCOMPARE(loaded.last()[0].toULongLong(), newest);
        QVERIFY(cache.cachedImage(path, {}));
    }

    void cancellationWinsOverUndeliveredCompletion()
    {
        QPromise<int> promise;
        promise.start();
        labelqt::ui::BusyTaskDialog dialog("Busy", "Cancel");
        int discarded = 0;
        QTimer::singleShot(0, &dialog, [&] {
            promise.addResult(42);
            promise.finish();
            dialog.reject();
        });
        const auto result = labelqt::ui::waitForBackgroundTask(promise.future(), dialog, [&] { ++discarded; });
        QVERIFY(!result);
        QCOMPARE(discarded, 1);
    }

    void backgroundTaskCancellationAndSuccess()
    {
        QPromise<int> promise;
        promise.start();
        labelqt::ui::BusyTaskDialog canceled("Busy", "Cancel");
        int discarded = 0;
        QTimer::singleShot(0, &canceled, &QDialog::reject);
        QVERIFY(!labelqt::ui::waitForBackgroundTask(promise.future(), canceled, [&] { ++discarded; }));
        QCOMPARE(discarded, 0);
        promise.addResult(42);
        promise.finish();
        QTRY_COMPARE(discarded, 1);
        labelqt::ui::BusyTaskDialog completed("Busy", "Cancel");
        const auto result = labelqt::ui::waitForBackgroundTask(promise.future(), completed);
        QVERIFY(result);
        QCOMPARE(*result, 42);
    }

    void automationPreparationRejectsReentry()
    {
        const auto root = QDir(QCoreApplication::applicationDirPath()).filePath("scripts/custom");
        QVERIFY(QDir().mkpath(root));
        QTemporaryDir scriptDir(root + "/risk-test-XXXXXX");
        QVERIFY(scriptDir.isValid());
        QVERIFY(AtomicFileWriter::write(scriptDir.filePath("entry.py"), "pass\n"));
        QVERIFY(AtomicFileWriter::write(scriptDir.filePath("script.json"), R"({
            "apiVersion": 2, "name": "Risk test", "scripts": [{
                "id": "risk_test", "name": "Risk test", "entry": "entry.py",
                "task": {"scope": "project", "read": ["text"], "write": []}
            }]})"));
        QWidget window;
        AutomationController controller(&window);
        const auto project = sample({});
        AutomationController::Callbacks callbacks;
        callbacks.isProjectEmpty = [] { return false; };
        callbacks.project = [&]() -> const Project& { return project; };
        controller.setCallbacks(std::move(callbacks));
        controller.refreshScripts();
        QString id;
        for (const auto& script : controller.scripts())
            if (script.directoryPath == scriptDir.path())
                id = script.id;
        QVERIFY(!id.isEmpty());
        QSignalSpy discoveries(&controller, &AutomationController::scriptsChanged);
        QSignalSpy states(&controller, &AutomationController::runningChanged);
        connect(&controller, &AutomationController::runningChanged, &controller, [&](bool running) {
            if (!running)
                return;
            QEventLoop loop;
            QTimer::singleShot(0, &loop, [&] {
                controller.refreshScripts();
                controller.runScriptById(id);
                controller.cancelRunningScript();
                loop.quit();
            });
            loop.exec();
        });
        controller.runScriptById(id);
        QVERIFY(!controller.isRunning());
        QCOMPARE(discoveries.size(), 0);
        QCOMPARE(states.size(), 2);
        QCOMPARE(states[0][0].toBool(), true);
        QCOMPARE(states[1][0].toBool(), false);
    }
};

QTEST_MAIN(RiskRegressionTests)
#include "RiskRegressionTests.moc"
