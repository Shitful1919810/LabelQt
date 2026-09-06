#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/FileRenameTransaction.h"
#include "services/project/ProjectController.h"
#include "services/project/ProjectWorkflowController.h"
#include "ui/automation/AutomationRunDialog.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>

#include <limits>
#include <stdexcept>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

Project sampleProject(const QTemporaryDir& directory)
{
    Project project;
    project.setFilePath(directory.filePath(QStringLiteral("project.txt")));
    project.setGroups({QStringLiteral("group")});
    for (int i = 1; i <= 3; ++i) {
        const QString name = QStringLiteral("%1.png").arg(i, 3, 10, QLatin1Char('0'));
        const QString path = directory.filePath(name);
        if (!AtomicFileWriter::write(path, QByteArray::number(i)))
            throw std::runtime_error("Fixture write failed");
        project.images().append({name, path, {Label(QStringLiteral("text"), QStringLiteral("group"), {})}});
    }
    return project;
}
} // namespace

class PersistenceSafetyTests final : public QObject {
    Q_OBJECT

private slots:
    void atomicWriteRejectsNonFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(!AtomicFileWriter::write(directory.path(), "data"));
#ifdef Q_OS_LINUX
        QVERIFY(!AtomicFileWriter::write(QStringLiteral("/dev/full"), "data"));
#endif
        const QString path = directory.filePath(QStringLiteral("data.txt"));
        QVERIFY(AtomicFileWriter::write(path, "before"));
        QVERIFY(AtomicFileWriter::write(path, "after"));
        QCOMPARE(readBytes(path), QByteArray("after"));
    }

    void textRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << QStringLiteral("  first  \n\n second \n-\nthird  ");
        QTest::newRow("source-name") << QString::fromUtf8("关联文件:这是一句正文\n下一行");
        QTest::newRow("trailing-newlines") << QStringLiteral("\nfirst\n\n\n");
        QTest::newRow("only-newlines") << QStringLiteral("\n\n");
        QTest::newRow("crlf") << QStringLiteral("a\r\nb\rc\n");
        QTest::newRow("spaces-only") << QStringLiteral(" \t \n \n");
        QTest::newRow("embedded-header") << QStringLiteral("Example: >>>>>>>>[page.png]<<<<<<<< is a header.");
    }

    void textRoundTrip()
    {
        QFETCH(QString, text);
        QTemporaryDir directory;
        Project project = sampleProject(directory);
        project.images()[0].labels[0].setText(text);
        project.images()[0].labels.prepend(Label("deleted", "group", {}));
        project.images()[0].labels[0].setDeleted(true);
        LabelPlusDocument::saveToFile(project, project.filePath());
        const Project loaded = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(loaded.images().size(), 3);
        QCOMPARE(loaded.images()[0].labels.size(), 1);
        QCOMPARE(loaded.images()[0].labels[0].text(), text);
        LabelPlusDocument::saveToFile(loaded, loaded.filePath());
        QCOMPARE(LabelPlusDocument::loadFromFile(loaded.filePath()).images()[0].labels[0].text(), text);
    }

    void reservedHeadersCannotOverwriteProject()
    {
        QTemporaryDir directory;
        Project project = sampleProject(directory);
        LabelPlusDocument::saveToFile(project, project.filePath());
        const QByteArray before = readBytes(project.filePath());
        for (const QString& text : {QStringLiteral(">>>>>>>>[page.png]<<<<<<<<"),
                                    QStringLiteral("----------------[1]----------------[0.1,0.2,1]")}) {
            project.images()[0].labels[0].setText(text);
            QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::saveToFile(project, project.filePath()), std::runtime_error);
            QCOMPARE(readBytes(project.filePath()), before);
        }
    }

    void textMetadataDoesNotUndoExternalEdits()
    {
        QTemporaryDir directory;
        Project project = sampleProject(directory);
        project.images()[0].labels[0].setText(QStringLiteral("original\n\n"));
        LabelPlusDocument::saveToFile(project, project.filePath());
        QByteArray bytes = readBytes(project.filePath());
        bytes.replace("original\n", "edited\n");
        QVERIFY(AtomicFileWriter::write(project.filePath(), bytes));
        QCOMPARE(LabelPlusDocument::loadFromFile(project.filePath()).images()[0].labels[0].text(),
                 QStringLiteral("edited"));
    }

    void damagedMetadataCannotBeSilentlyDiscarded()
    {
        QTemporaryDir directory;
        Project project = sampleProject(directory);
        LabelPlusDocument::saveToFile(project, project.filePath());
        const QByteArray before = readBytes(project.filePath());
        project.setCommentLines({"# LabelQtMetadata v1", "# payload=broken", "# EndLabelQtMetadata"});
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::saveToFile(project, project.filePath()), std::runtime_error);
        QCOMPARE(readBytes(project.filePath()), before);
        QByteArray broken = before;
        broken.replace("group\n-\n", "group\n-\n# LabelQtMetadata v1\n# payload=broken\n# EndLabelQtMetadata\n");
        QVERIFY(AtomicFileWriter::write(project.filePath(), broken));
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::loadFromFile(project.filePath()), std::runtime_error);
        QCOMPARE(readBytes(project.filePath()), broken);
    }

    void saveFailureKeepsDirtyAndUndo()
    {
        QTemporaryDir directory;
        ProjectController controller;
        controller.setProject(sampleProject(directory));
        controller.project().setFilePath(directory.path());
        controller.markDirty();
        UndoStack undo;
        undo.push("edit", [] {});
        ProjectWorkflowController workflow(controller, undo, "reorder");
        const auto plan = ProjectImageRenameService::createPlan(controller.project(), {"{index:03}", 2, {}});
        QVERIFY(!workflow.applyImageRename(plan));
        QVERIFY(controller.isDirty());
        QVERIFY(undo.canUndo());
        QCOMPARE(readBytes(directory.filePath("001.png")), QByteArray("1"));
    }

    void rollbackChainsAndCycles_data()
    {
        QTest::addColumn<bool>("cycle");
        QTest::addColumn<int>("failedCall");
        for (bool cycle : {false, true}) {
            for (int call = 1; call <= 6; ++call) {
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(cycle ? "cycle" : "chain").arg(call)))
                    << cycle << call;
            }
        }
    }

    void rollbackChainsAndCycles()
    {
        QFETCH(bool, cycle);
        QFETCH(int, failedCall);
        QTemporaryDir directory;
        const Project project = sampleProject(directory);
        QVector<FileRenameTransaction::Move> moves;
        for (int i = 0; i < 3; ++i) {
            moves.append({project.images()[i].path, i == 2 && cycle ? project.images()[0].path
                                                                    : directory.filePath(QStringLiteral("%1.png").arg(
                                                                          i + 2, 3, 10, QLatin1Char('0')))});
        }
        int calls = 0;
        FileRenameTransaction transaction(std::move(moves), directory.path(),
                                          [&](const QString& from, const QString& to) {
                                              if (++calls == failedCall)
                                                  return false;
                                              return QFile::rename(from, to);
                                          });
        QVERIFY(!transaction.apply());
        QVERIFY(transaction.rollback().isEmpty());
        for (int i = 0; i < 3; ++i)
            QCOMPARE(readBytes(project.images()[i].path), QByteArray::number(i + 1));
        QVERIFY(!QFile::exists(transaction.recoveryPath()));
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size(), 3);
    }

    void rollbackReportsBlockedRestoreAndKeepsEvidence()
    {
        QTemporaryDir directory;
        const Project project = sampleProject(directory);
        LabelPlusDocument::saveToFile(project, project.filePath());
        const QByteArray savedProject = readBytes(project.filePath());
        const QString target = directory.filePath("target.png");
        FileRenameTransaction transaction({{project.images()[0].path, target}}, directory.path());
        QVERIFY(transaction.apply());
        QVERIFY(AtomicFileWriter::write(project.images()[0].path, "external"));
        QVERIFY(!transaction.rollback().isEmpty());
        QCOMPARE(readBytes(project.images()[0].path), QByteArray("external"));
        QVERIFY(QFile::exists(transaction.recoveryPath()));
        const QStringList temporaryFiles = QDir(directory.path()).entryList({"*.tmp"}, QDir::Files | QDir::Hidden);
        QCOMPARE(temporaryFiles.size(), 1);
        QCOMPARE(readBytes(directory.filePath(temporaryFiles.first())), QByteArray("1"));
        ProjectController controller;
        controller.setProject(project);
        controller.markDirty();
        QVERIFY_EXCEPTION_THROWN(controller.save(), std::runtime_error);
        QVERIFY(controller.isDirty());
        QVERIFY_EXCEPTION_THROWN(controller.loadFromFile(project.filePath()), std::runtime_error);
        QCOMPARE(readBytes(project.filePath()), savedProject);
    }

    void postSaveFailureRollsBackFilesAndMemory_data()
    {
        QTest::addColumn<bool>("throws");
        QTest::newRow("expected-error") << false;
        QTest::newRow("exception") << true;
    }

    void postSaveFailureRollsBackFilesAndMemory()
    {
        QFETCH(bool, throws);
        QTemporaryDir directory;
        Project project = sampleProject(directory);
        LabelPlusDocument::saveToFile(project, project.filePath());
        const QByteArray before = readBytes(project.filePath());
        const auto plan = ProjectImageRenameService::createPlan(project, {"{index:03}", 2, {}});
        const auto result = ProjectImageRenameService::applyPlan(
            project, plan, [throws](const Project&) -> std::expected<void, QString> {
                if (throws)
                    throw std::runtime_error("Injected save error");
                return std::unexpected(QStringLiteral("Injected save error"));
            });
        QVERIFY(!result);
        QCOMPARE(readBytes(project.filePath()), before);
        QCOMPARE(project.images()[0].name, QStringLiteral("001.png"));
        for (int i = 0; i < 3; ++i)
            QCOMPARE(readBytes(project.images()[i].path), QByteArray::number(i + 1));
        QCOMPARE(QDir(directory.path()).entryList({".labelqt-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void renameCommitsProjectBeforeClearingUndo()
    {
        QTemporaryDir directory;
        ProjectController controller;
        controller.setProject(sampleProject(directory));
        controller.project().images()[0].labels[0].setText(QStringLiteral("unsaved edit\n\n"));
        controller.markDirty();
        UndoStack undo;
        undo.push("edit", [] {});
        ProjectWorkflowController workflow(controller, undo, "reorder");
        const auto plan = ProjectImageRenameService::createPlan(controller.project(), {"{index:03}", 2, {}});
        QVERIFY(workflow.applyImageRename(plan));
        QVERIFY(!controller.isDirty());
        QVERIFY(!undo.canUndo());
        const Project loaded = LabelPlusDocument::loadFromFile(controller.project().filePath());
        QCOMPARE(loaded.images()[0].name, QStringLiteral("002.png"));
        QCOMPARE(loaded.images()[0].labels[0].text(), QStringLiteral("unsaved edit\n\n"));
        QCOMPARE(readBytes(loaded.images()[0].path), QByteArray("1"));
    }

    void extremePatternsAreRejected()
    {
        QVERIFY(!ProjectImageRenameService::analyzePattern(QStringLiteral("{index:099999999999999999}")).isValid());
        QVERIFY(!ProjectImageRenameService::analyzePattern(QStringLiteral("{index:0100000000}")).isValid());
        QTemporaryDir directory;
        QVERIFY(!ProjectImageRenameService::createPlan(sampleProject(directory),
                                                       {"{index}", std::numeric_limits<int>::max(), {}})
                     .isValid());
    }

    void escapeHidesRunningLogWithoutDestroyingIt()
    {
        QPointer<AutomationRunDialog> dialog = new AutomationRunDialog(QStringLiteral("test"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        QTest::keyClick(dialog, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog);
        QVERIFY(!dialog->isVisible());
        dialog->show();
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog);
        dialog->setFinished(true);
        dialog->reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!dialog);
    }
};

QTEST_MAIN(PersistenceSafetyTests)
#include "PersistenceSafetyTests.moc"
