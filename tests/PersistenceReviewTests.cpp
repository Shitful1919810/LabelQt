#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "core/ProjectMetadataService.h"
#include "services/io/FileRenameTransaction.h"
#include "services/io/ProjectFileTransaction.h"
#include "services/project/ProjectImageRenameService.h"
#include "services/project/ProjectRecoveryService.h"
#include "services/project/ProjectWorkflowController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTest>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray classic()
{
    return "1,0\n-\ngroup\n-\n>>>>>>>>[001.png]<<<<<<<<\n"
           "----------------[1]----------------[0.1,0.2,1]\ntext\n\n";
}

Project fixture(const QTemporaryDir& dir)
{
    for (const auto& child : {"a", "b", "images"})
        if (!QDir(dir.path()).mkdir(child))
            qFatal("Cannot create fixture directory");
    Project project;
    project.setFilePath(dir.filePath("a/project.txt"));
    project.setGroups({"group"});
    for (int i = 1; i <= 2; ++i) {
        const auto name = QStringLiteral("%1.png").arg(i, 3, 10, QLatin1Char('0'));
        const auto path = dir.filePath("images/" + name);
        if (!AtomicFileWriter::writeNew(path, QByteArray::number(i)))
            qFatal("Cannot create fixture image");
        project.images().append({name, path, {Label(QString::number(i), "group", {})}});
    }
    LabelPlusDocument::saveToFile(project, project.filePath());
    return project;
}

bool competingProcessRejected(const Project& project, const QString& journalDirectory)
{
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(),
                  {"--competing-rename", project.images()[0].path, project.images()[1].path, journalDirectory});
    return process.waitForStarted(5000) && process.waitForFinished(10000) &&
           process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void makeNewer(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite) ||
        !file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(60), QFileDevice::FileModificationTime))
        qFatal("Cannot set fixture timestamp");
}
} // namespace

class PersistenceReviewTests final : public QObject {
    Q_OBJECT
private slots:
    void directoryLockCoversProjectCommitAcrossProcesses()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        const auto original = project;
        const auto plan = ProjectImageRenameService::createPlan(project, {"{index:03}", 1, {1, 0}});
        QVERIFY(plan.isValid());
        ProjectFileTransaction projectLock(project.filePath());
        const auto revision = FileRevision::read(project.filePath()).value();
        bool rejectedBeforeSave = false, rejectedAfterSave = false;
        const auto result = ProjectImageRenameService::applyPlan(
            project, plan, [&](const Project& candidate) -> std::expected<void, QString> {
                rejectedBeforeSave = competingProcessRejected(original, dir.filePath("b"));
                (void)projectLock.write(LabelPlusDocument::serialize(candidate).toUtf8(), revision);
                rejectedAfterSave = competingProcessRejected(original, dir.filePath("b"));
                return {};
            });
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QVERIFY(rejectedBeforeSave);
        QVERIFY(rejectedAfterSave);
        QCOMPARE(bytes(project.images()[0].path), QByteArray("1"));
        QCOMPARE(bytes(project.images()[1].path), QByteArray("2"));
        const auto loaded = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(loaded.images()[0].name, project.images()[0].name);
        RenameDirectoryLocks released;
        QVERIFY(released.acquire({dir.filePath("images"), dir.filePath("a")}));
    }

    void directoryLockCoversRollbackAndReleasesAfterFailure()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto a = project.images()[0].path, b = project.images()[1].path;
        bool rollingBack = false, allBlocked = true;
        int attempts = 0;
        FileRenameTransaction transaction({{a, b}, {b, a}}, dir.filePath("a"),
                                          [&](const QString& from, const QString& to) {
                                              if (rollingBack) {
                                                  ++attempts;
                                                  RenameDirectoryLocks competitor;
                                                  allBlocked &= !competitor.acquire({dir.filePath("images")});
                                              }
                                              return QFile::rename(from, to);
                                          });
        QVERIFY(transaction.apply());
        rollingBack = true;
        QVERIFY(transaction.rollback().isEmpty());
        QCOMPARE(attempts, 4);
        QVERIFY(allBlocked);
        QCOMPARE(bytes(a), QByteArray("1"));
        QCOMPARE(bytes(b), QByteArray("2"));
        RenameDirectoryLocks released;
        QVERIFY(released.acquire({dir.filePath("images")}));
    }

    void recoveryUsesImageDirectoryLock_data()
    {
        QTest::addColumn<bool>("complete");
        QTest::newRow("rollback") << false;
        QTest::newRow("complete") << true;
    }

    void recoveryUsesImageDirectoryLock()
    {
        QFETCH(bool, complete);
        QTemporaryDir dir;
        const auto project = fixture(dir);
        auto updated = project;
        updated.images()[0].name = "renamed.png";
        updated.images()[0].path = dir.filePath("images/renamed.png");
        const auto before = bytes(project.filePath());
        FileRenameTransaction transaction({{project.images()[0].path, updated.images()[0].path}}, dir.filePath("a"));
        transaction.setProjectCommit(project.filePath(), before, LabelPlusDocument::serialize(updated).toUtf8());
        QVERIFY(transaction.apply());
        const auto recordPath = transaction.recoveryPath();
        const auto record = bytes(recordPath);
        transaction.commit();
        QVERIFY(AtomicFileWriter::writeNew(recordPath, record));
        RenameDirectoryLocks other;
        QVERIFY(other.acquire({dir.filePath("images")}));
        QVERIFY(!ProjectRecoveryService::recoverFiles(recordPath, complete));
        QCOMPARE(bytes(recordPath), record);
        QCOMPARE(bytes(project.filePath()), before);
        QVERIFY(!QFile::exists(project.images()[0].path));
        QCOMPARE(bytes(updated.images()[0].path), QByteArray("1"));
        other.release();
        QVERIFY(ProjectRecoveryService::recoverFiles(recordPath, complete));
        QVERIFY(!QFile::exists(recordPath));
        const auto loaded = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(loaded.images()[0].path, complete ? updated.images()[0].path : project.images()[0].path);
        QCOMPARE(bytes(loaded.images()[0].path), QByteArray("1"));
        RenameDirectoryLocks released;
        QVERIFY(released.acquire({dir.filePath("images")}));
    }

    void directoryAliasesAndPartialAcquisition()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("a"));
        QVERIFY(QDir(dir.path()).mkdir("b"));
        RenameDirectoryLocks owner;
        QVERIFY(owner.acquire({dir.filePath("b")}));
        RenameDirectoryLocks failed;
        QVERIFY(!failed.acquire({dir.filePath("b"), dir.filePath("a")}));
        RenameDirectoryLocks free;
        QVERIFY(free.acquire({dir.filePath("a"), dir.filePath("./a")}));
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(dir.filePath("b"), dir.filePath("alias")));
        RenameDirectoryLocks alias;
        QVERIFY(!alias.acquire({dir.filePath("alias")}));
#endif
    }

    void mergedOutputCannotInvalidateRenameJournal()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        auto output = project;
        output.images()[0].labels[0].setText("merged edits");
        ProjectController controller;
        UndoStack undo;
        ProjectWorkflowController workflow(controller, undo, "reorder");
        const auto before = bytes(project.filePath());
        auto renamed = project;
        renamed.images()[0].name = "renamed.png";
        renamed.images()[0].path = dir.filePath("images/renamed.png");
        FileRenameTransaction transaction({{project.images()[0].path, renamed.images()[0].path}}, dir.filePath("a"));
        transaction.setProjectCommit(project.filePath(), before, LabelPlusDocument::serialize(renamed).toUtf8());
        QVERIFY(transaction.apply());
        const auto journal = transaction.recoveryPath();
        const auto evidence = bytes(journal);
        transaction.commit();
        QVERIFY(AtomicFileWriter::writeNew(journal, evidence));
        const auto previous = ProjectFileTransaction::previousVersionPath(project.filePath());
        QVERIFY_EXCEPTION_THROWN(workflow.saveProject(output, project.filePath()), std::runtime_error);
        QCOMPARE(bytes(project.filePath()), before);
        QVERIFY(!QFile::exists(previous));
        QCOMPARE(bytes(journal), evidence);
        QVERIFY(ProjectRecoveryService::recoverFiles(journal, false));
        workflow.saveProject(output, project.filePath());
        QCOMPARE(bytes(previous), before);
        QCOMPARE(LabelPlusDocument::loadFromFile(project.filePath()).images()[0].labels[0].text(), "merged edits");
    }

    void strictDecodingPreservesBomAndDigest_data()
    {
        QTest::addColumn<int>("encoding");
        QTest::addColumn<bool>("bom");
        QTest::newRow("utf8") << int(QStringConverter::Utf8) << false;
        QTest::newRow("utf8-bom") << int(QStringConverter::Utf8) << true;
        QTest::newRow("utf16le") << int(QStringConverter::Utf16LE) << true;
        QTest::newRow("utf16be") << int(QStringConverter::Utf16BE) << true;
        QTest::newRow("utf32le") << int(QStringConverter::Utf32LE) << true;
        QTest::newRow("utf32be") << int(QStringConverter::Utf32BE) << true;
    }

    void strictDecodingPreservesBomAndDigest()
    {
        QFETCH(int, encoding);
        QFETCH(bool, bom);
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        const auto body = QString::fromUtf16(u"\u4e2d\u6587\U0001d11e");
        const auto text = QString::fromUtf8(classic()).replace("text", body);
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding),
                               bom ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default);
        const QByteArray raw = encoder(text);
        QVERIFY(AtomicFileWriter::writeNew(path, raw));
        const auto loaded = LabelPlusDocument::readFromFile(path);
        const auto digest = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
        QCOMPARE(loaded.sha256, digest);
        QCOMPARE(loaded.project.images()[0].labels[0].text(), body);
        ProjectRecoveryEntry entry{ProjectRecoveryEntry::Kind::Backup, path, path, path, {}, digest, false};
        const auto backup = ProjectRecoveryService::preview(entry);
        QVERIFY(backup);
        QCOMPARE(backup->images()[0].labels[0].text(), loaded.project.images()[0].labels[0].text());
    }

    void malformedEncodingRejectedByDocumentsAndBackups_data()
    {
        QTest::addColumn<QByteArray>("raw");
        QTest::newRow("utf8-invalid") << classic().replace("text", QByteArray::fromHex("ff"));
        QTest::newRow("utf8-truncated") << (classic() + QByteArray::fromHex("e4b8"));
        QTest::newRow("utf8-overlong") << (classic() + QByteArray::fromHex("c080"));
        QTest::newRow("utf8-surrogate") << (classic() + QByteArray::fromHex("eda080"));
        QTest::newRow("nul-body") << classic().replace("text", QByteArray("a\0b", 3));
        QTest::newRow("nul-comment") << classic().replace("group\n-\n", QByteArray("group\n-\n# a\0b\n", 14));
        QStringEncoder utf16(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom);
        const QByteArray utf16Bytes = utf16(QString::fromUtf8(classic()));
        QTest::newRow("utf16-truncated") << (utf16Bytes + QByteArray::fromHex("00"));
        QTest::newRow("utf16-surrogate") << (utf16Bytes + QByteArray::fromHex("00d8"));
        QStringEncoder utf32(QStringConverter::Utf32LE, QStringConverter::Flag::WriteBom);
        const QByteArray utf32Bytes = utf32(QString::fromUtf8(classic()));
        QTest::newRow("utf32-truncated") << (utf32Bytes + QByteArray::fromHex("00"));
        QTest::newRow("utf32-out-of-range") << (utf32Bytes + QByteArray::fromHex("00001100"));
    }

    void malformedEncodingRejectedByDocumentsAndBackups()
    {
        QFETCH(QByteArray, raw);
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        QVERIFY(AtomicFileWriter::writeNew(path, raw));
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::readFromFile(path), std::runtime_error);
        ProjectRecoveryEntry entry{ProjectRecoveryEntry::Kind::Backup,
                                   path,
                                   path,
                                   path,
                                   {},
                                   QCryptographicHash::hash(raw, QCryptographicHash::Sha256),
                                   false};
        QVERIFY(!ProjectRecoveryService::preview(entry));
        QVERIFY(!ProjectRecoveryService::restoreCopy(entry, dir.filePath("copy.txt")));
        QVERIFY(!QFile::exists(dir.filePath("copy.txt")));
        QCOMPARE(bytes(path), raw);
    }

    void htmlCharsetCannotOverrideUtf8()
    {
        const auto body = QString::fromUtf16(u"<meta charset=\"windows-1252\">\n\u4e2d\u6587");
        auto raw = classic();
        raw.replace("text", body.toUtf8());
        const auto loaded = LabelPlusDocument::readFromBytes(raw, "/project.txt");
        QCOMPARE(loaded.project.images()[0].labels[0].text(), body);
        raw.replace(body.toUtf8(), QByteArray("<meta charset=\"windows-1252\">\n\xe9"));
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::readFromBytes(raw, "/project.txt"), std::runtime_error);
    }

    void invalidGroupInPreviousVersionDoesNotInterruptOpening()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        QVERIFY(AtomicFileWriter::writeNew(path, classic()));
        auto damaged = classic();
        damaged.replace("group", QByteArray("gro\0up", 6));
        const auto previous = ProjectFileTransaction::previousVersionPath(path);
        QVERIFY(AtomicFileWriter::writeNew(previous, damaged));
        makeNewer(previous);
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::readFromBytes(damaged, previous), std::runtime_error);
        QVERIFY(!ProjectRecoveryService::hasNewerRecovery(path, AppPreferences{}));
        ProjectController controller;
        controller.loadFromFile(path);
        QCOMPARE(controller.project().images().size(), 1);
        QCOMPARE(bytes(previous), damaged);
        QCOMPARE(ProjectRecoveryService::entries(path, AppPreferences{}).size(), 1);
    }

    void nulCannotBeSavedIntoAnUnreadableDocument()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto original = bytes(project.filePath());
        for (int field = 0; field < 4; ++field) {
            auto invalid = project;
            const auto nul = QString::fromUtf8("a\0b", 3);
            if (field == 0)
                invalid.images()[0].labels[0].setText(nul);
            else if (field == 1)
                invalid.images()[0].name = nul;
            else if (field == 2)
                invalid.setSourceName(nul);
            else
                invalid.setCommentLines({nul});
            QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::saveToFile(invalid, project.filePath()), std::runtime_error);
            QCOMPARE(bytes(project.filePath()), original);
        }
    }

    void isolatedSurrogateCannotOverwriteProject_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("malformed");
        for (const auto& field :
             QStringList{"text", "group", "page-name", "source", "comment", "image-path", "label-group"}) {
            QTest::newRow(qPrintable(field + "-high"))
                << field << (QStringLiteral("before") + QChar(char16_t(0xd800)) + QStringLiteral("after"));
            QTest::newRow(qPrintable(field + "-low"))
                << field << (QStringLiteral("before") + QChar(char16_t(0xdc00)) + QStringLiteral("after"));
        }
    }

    void isolatedSurrogateCannotOverwriteProject()
    {
        QFETCH(QString, field);
        QFETCH(QString, malformed);
        QVERIFY(!malformed.isValidUtf16());
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto original = bytes(project.filePath());
        auto invalid = project;
        if (field == "text")
            invalid.images()[0].labels[0].setText(malformed);
        else if (field == "group") {
            QVERIFY(!LabelPlusDocument::isValidGroupName(malformed));
            invalid.setGroups({malformed});
        }
        else if (field == "page-name")
            invalid.images()[0].name = malformed;
        else if (field == "source")
            invalid.setSourceName(malformed);
        else if (field == "comment")
            invalid.setCommentLines({malformed});
        else if (field == "image-path")
            invalid.images()[0].path = dir.filePath(malformed);
        else
            invalid.images()[0].labels[0].setGroup(malformed);
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::saveToFile(invalid, project.filePath()), std::runtime_error);
        QCOMPARE(bytes(project.filePath()), original);
    }

    void serializationFailureInRecoveryNoticeIsIsolated()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.txt");
        QVERIFY(AtomicFileWriter::writeNew(path, classic()));
        QJsonObject metadata{{"padding", ""}};
        const auto overhead = QJsonDocument(metadata).toJson(QJsonDocument::Compact).size();
        metadata.insert("padding", QString(ProjectMetadataService::maxDecodedBytes - overhead, QLatin1Char('x')));
        const auto comments = ProjectMetadataService::rewriteCommentLines({}, metadata).join('\n').toUtf8();
        auto raw = classic();
        raw.replace("group\n-\n", "group\n-\n" + comments + '\n');
        const auto previous = ProjectFileTransaction::previousVersionPath(path);
        QVERIFY(AtomicFileWriter::writeNew(previous, raw));
        makeNewer(previous);
        const auto parsed = LabelPlusDocument::readFromFile(previous);
        QVERIFY_EXCEPTION_THROWN(LabelPlusDocument::serialize(parsed.project), std::runtime_error);
        QVERIFY(!ProjectRecoveryService::hasNewerRecovery(path, AppPreferences{}));
        QCOMPARE(bytes(previous), raw);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 5 && arguments[1] == "--competing-rename") {
        ProjectFileTransaction projectLock(QDir(arguments[4]).filePath("other.txt"));
        FileRenameTransaction transaction({{arguments[2], arguments[3]}, {arguments[3], arguments[2]}}, arguments[4]);
        return transaction.apply() ? 1 : 0;
    }
    PersistenceReviewTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "PersistenceReviewTests.moc"
