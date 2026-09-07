#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/BoundedJsonIo.h"
#include "services/io/FileRenameTransaction.h"
#include "services/project/ProjectRecoveryService.h"
#include "services/proofread/ReviewSessionService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <limits>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

Project fixture(const QTemporaryDir& dir)
{
    Project project;
    project.setFilePath(dir.filePath("project.txt"));
    project.setGroups({"dialogue"});
    project.images().append({"old.png", dir.filePath("old.png"), {Label("hello", "dialogue", {0.2, 0.3})}});
    if (!AtomicFileWriter::write(project.images()[0].path, "image"))
        qFatal("Cannot create source file");
    LabelPlusDocument::saveToFile(project, project.filePath());
    return project;
}

QJsonObject journal(const Project& project, const QTemporaryDir& dir)
{
    const auto snapshot = QString::fromLatin1(LabelPlusDocument::serialize(project).toUtf8().toBase64());
    return {
        {"version", 2},
        {"project", project.filePath()},
        {"before", snapshot},
        {"after", snapshot},
        {"moves",
         QJsonArray{QJsonObject{
             {"source", project.images()[0].path},
             {"target", dir.filePath("new.png")},
             {"temporary", dir.filePath("stage.tmp")},
             {"sha256", QString::fromLatin1(QCryptographicHash::hash("image", QCryptographicHash::Sha256).toHex())}}}}};
}

QString localPath(const QString& key)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(
            QStringLiteral("review-sessions/%1.json")
                .arg(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex())));
}
} // namespace

class RecordStorageTests final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_storage;

private slots:
    void initTestCase()
    {
        QVERIFY(m_storage.isValid());
        qputenv("XDG_DATA_HOME", m_storage.path().toUtf8());
        QCoreApplication::setOrganizationName("LabelQtTests");
        QCoreApplication::setApplicationName("RecordStorage-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    void cleanupTestCase()
    {
        QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        if (directory.exists())
            QVERIFY(directory.removeRecursively());
    }

    void boundedJsonSymmetry()
    {
        const QJsonObject object{{"text", "escaped \\\" [{}]"}, {"items", QJsonArray{1, 2}}};
        const auto encoded = QJsonDocument(object).toJson();
        const BoundedJsonIo::Limits limits{encoded.size(), 5, 3};
        const QString error = "invalid";
        QVERIFY(BoundedJsonIo::encode(object, limits, error));
        QVERIFY(BoundedJsonIo::parse(encoded, limits, error));
        QTemporaryDir dir;
        const auto path = dir.filePath("record.json");
        QVERIFY(AtomicFileWriter::write(path, encoded));
        QVERIFY(BoundedJsonIo::read(path, limits, error));
        auto smaller = limits;
        --smaller.bytes;
        QVERIFY(!BoundedJsonIo::encode(object, smaller, error));
        QVERIFY(!BoundedJsonIo::read(path, smaller, error));
        smaller = limits;
        --smaller.items;
        QVERIFY(!BoundedJsonIo::encode(object, smaller, error));
        QVERIFY(!BoundedJsonIo::read(path, smaller, error));
        smaller = limits;
        --smaller.depth;
        QVERIFY(!BoundedJsonIo::encode(object, smaller, error));
        QVERIFY(!BoundedJsonIo::read(path, smaller, error));
        for (const auto bad : {BoundedJsonIo::Limits{0, 1},
                               {-1, 1},
                               {1, 0},
                               {1, -1},
                               {1, 1, 0},
                               {std::numeric_limits<qint64>::max(), 1}}) {
            QVERIFY(!BoundedJsonIo::encode(object, bad, error));
            QVERIFY(!BoundedJsonIo::read(path, bad, error));
        }
        for (const QByteArray& bad :
             {QByteArray("[]"), QByteArray("{broken"), QByteArray("{} trailing"), QByteArray("{\"a\":[[[[1]]]]}")})
            QVERIFY(!BoundedJsonIo::parse(bad, limits, error));
    }

    void reviewNotesAndPairsRoundTrip()
    {
        ReviewSession session;
        session.annotations.insert("legacy-key",
                                   {ReviewStatus::NeedsConfirmation, QString::fromUtf8("旧批注\nメモ\n")});
        session.pairs.append(ManualLabelPair{{"old.png", "before", "dialogue", {0.2, 0.3}, 0},
                                             {"new.png", "after", "dialogue", {0.4, 0.5}, 1}});
        QVERIFY(ReviewSessionService::validate(session));
        QVERIFY(ReviewSessionService::saveLocal("notes", session));
        const auto loaded = ReviewSessionService::loadLocal("notes");
        QVERIFY(loaded);
        QCOMPARE(ReviewSessionService::encode(*loaded), ReviewSessionService::encode(session));
        auto changed = *loaded;
        changed.annotations["legacy-key"].status = ReviewStatus::Reviewed;
        QVERIFY(ReviewSessionService::saveLocal("notes", changed));
        QCOMPARE(ReviewSessionService::loadLocal("notes")->annotations["legacy-key"].note,
                 session.annotations["legacy-key"].note);
        QVERIFY(ReviewSessionService::decode({}));
        QVERIFY(ReviewSessionService::loadLocal("missing"));
        Project project;
        project.setCommentLines(ReviewSessionService::withProjectSession({}, ReviewSessionService::encode(changed)));
        const auto projectSession = ReviewSessionService::decode(ReviewSessionService::projectSession(project));
        QVERIFY(projectSession);
        QVERIFY(ReviewSessionService::validate(*projectSession));
        QCOMPARE(ReviewSessionService::encode(*projectSession), ReviewSessionService::encode(changed));
    }

    void reviewRejectsInvalidWithoutOverwriting()
    {
        ReviewSession session;
        session.annotations.insert("key", {ReviewStatus::Reviewed, "keep me"});
        QVERIFY(ReviewSessionService::saveLocal("invalid", session));
        const auto path = localPath("invalid");
        const auto original = bytes(path);
        session.annotations["key"].status = static_cast<ReviewStatus>(10);
        QVERIFY(!ReviewSessionService::validate(session));
        QVERIFY(!ReviewSessionService::saveLocal("invalid", session));
        QCOMPARE(bytes(path), original);
        session.annotations["key"].status = ReviewStatus::Reviewed;
        session.pairs.append(ManualLabelPair{});
        session.pairs[0].current.position.setX(std::numeric_limits<double>::infinity());
        QVERIFY(!ReviewSessionService::validate(session));
        QVERIFY(!ReviewSessionService::saveLocal("invalid", session));
        QCOMPARE(bytes(path), original);
        session.pairs.clear();
        session.annotations["key"].note = QString(BoundedJsonIo::SnapshotBytes + 1, QLatin1Char('n'));
        QVERIFY(!ReviewSessionService::validate(session));
        QVERIFY(!ReviewSessionService::saveLocal("invalid", session));
        QCOMPARE(bytes(path), original);
        const auto tooLarge = ReviewSessionService::encode(session);
        QVERIFY(!ReviewSessionService::decode(tooLarge));
        const QJsonObject pair{{"before", QJsonObject{}}, {"after", QJsonObject{}}};
        for (const auto& bad :
             {QJsonObject{{"version", 1.5}, {"annotations", QJsonObject{}}, {"pairs", QJsonArray{}}},
              QJsonObject{{"version", 1}, {"annotations", QJsonObject{}}, {"pairs", QJsonArray{pair}}},
              QJsonObject{{"version", 1},
                          {"annotations", QJsonObject{{"key", QJsonObject{{"status", 0.5}, {"note", "keep"}}}}},
                          {"pairs", QJsonArray{}}}}) {
            QVERIFY(!ReviewSessionService::decode(bad));
            QVERIFY(AtomicFileWriter::write(path, QJsonDocument(bad).toJson()));
            QVERIFY(!ReviewSessionService::loadLocal("invalid"));
        }
    }

    void reviewItemBoundary()
    {
        ReviewSession session;
        for (int i = 0; i < BoundedJsonIo::RecordItems; ++i)
            session.annotations.insert(QString::number(i), {ReviewStatus::Unseen, "note"});
        QVERIFY(ReviewSessionService::validate(session));
        QVERIFY(ReviewSessionService::saveLocal("items", session));
        const auto loaded = ReviewSessionService::loadLocal("items");
        QVERIFY(loaded);
        QCOMPARE(loaded->annotations.size(), BoundedJsonIo::RecordItems);
        const auto original = bytes(localPath("items"));
        session.pairs.append(ManualLabelPair{});
        QVERIFY(!ReviewSessionService::validate(session));
        QVERIFY(!ReviewSessionService::saveLocal("items", session));
        QCOMPARE(bytes(localPath("items")), original);
        QVERIFY(!ReviewSessionService::decode(ReviewSessionService::encode(session)));
    }

    void reviewByteBoundary()
    {
        ReviewSession session;
        session.annotations.insert("key", {ReviewStatus::Reviewed, {}});
        const auto overhead = QJsonDocument(ReviewSessionService::encode(session)).toJson().size();
        session.annotations["key"].note = QString(BoundedJsonIo::Review.bytes - overhead, QLatin1Char('n'));
        QVERIFY(ReviewSessionService::validate(session));
        QVERIFY(ReviewSessionService::saveLocal("bytes", session));
        QCOMPARE(QFileInfo(localPath("bytes")).size(), BoundedJsonIo::Review.bytes);
        const auto loaded = ReviewSessionService::loadLocal("bytes");
        QVERIFY(loaded);
        QCOMPARE(loaded->annotations["key"].note, session.annotations["key"].note);
        session.annotations["key"].note.append(QLatin1Char('n'));
        QVERIFY(!ReviewSessionService::validate(session));
        QVERIFY(!ReviewSessionService::saveLocal("bytes", session));
        QCOMPARE(QFileInfo(localPath("bytes")).size(), BoundedJsonIo::Review.bytes);
        QVERIFY(!ReviewSessionService::decode(ReviewSessionService::encode(session)));
    }

    void nonRegularAndOversizeFiles()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("record.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(BoundedJsonIo::MaximumBytes + 1));
        file.close();
        QVERIFY(!ProjectRecoveryService::recoveryDetails(path));
        QVERIFY(!ProjectRecoveryService::recoverFiles(path, false));
        QVERIFY(!BoundedJsonIo::read(dir.path(), BoundedJsonIo::Rename, "invalid"));
        QVERIFY(QDir().mkpath(QFileInfo(localPath("large")).absolutePath()));
        QFile review(localPath("large"));
        QVERIFY(review.open(QIODevice::WriteOnly));
        QVERIFY(review.resize(BoundedJsonIo::Review.bytes + 1));
        review.close();
        QVERIFY(!ReviewSessionService::loadLocal("large"));
#ifdef Q_OS_UNIX
        const auto link = localPath("link");
        QVERIFY(QFile::link(path, link));
        QVERIFY(!ReviewSessionService::loadLocal("link"));
        QVERIFY(!ReviewSessionService::saveLocal("link", {}));
        QVERIFY(!ProjectRecoveryService::recoveryDetails(link));
        const auto dangling = localPath("dangling");
        QVERIFY(QFile::link(dir.filePath("absent"), dangling));
        QVERIFY(!ReviewSessionService::loadLocal("dangling"));
        QVERIFY(!ReviewSessionService::saveLocal("dangling", {}));
        const auto fifo = dir.filePath("pipe.json");
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        QVERIFY(!ProjectRecoveryService::recoveryDetails(fifo));
        QVERIFY(!BoundedJsonIo::writableFile(fifo));
#endif
    }

    void journalWriteRejectsBeforeMutation()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto snapshot = LabelPlusDocument::serialize(project).toUtf8();
        int calls = 0;
        FileRenameTransaction transaction({{project.images()[0].path, dir.filePath("new.png")}}, dir.path(),
                                          [&](const QString&, const QString&) {
                                              ++calls;
                                              return true;
                                          });
        transaction.setProjectCommit(project.filePath(), snapshot, QByteArray(BoundedJsonIo::SnapshotBytes + 1, 'x'));
        QVERIFY(!transaction.apply());
        QCOMPARE(calls, 0);
        QVERIFY(!QFileInfo::exists(transaction.recoveryPath()));
        QCOMPARE(bytes(project.images()[0].path), QByteArray("image"));
        QVector<FileRenameTransaction::Move> moves(BoundedJsonIo::RecordItems + 1,
                                                   {project.images()[0].path, dir.filePath("new.png")});
        FileRenameTransaction tooMany(std::move(moves), dir.path());
        QVERIFY(!tooMany.apply());
        QVERIFY(!QFileInfo::exists(tooMany.recoveryPath()));
        FileRenameTransaction invalid({{project.images()[0].path, dir.filePath("new.png")}}, dir.path());
        invalid.setProjectCommit(project.filePath(), snapshot, "not a project");
        QVERIFY(!invalid.apply());
        QVERIFY(!QFileInfo::exists(invalid.recoveryPath()));
        const auto retainedPath = invalid.recoveryPath();
        QVERIFY(AtomicFileWriter::write(retainedPath, "external evidence"));
        QVERIFY(invalid.rollback().isEmpty());
        QCOMPARE(bytes(retainedPath), QByteArray("external evidence"));
    }

    void journalRecoveryRoundTrip_data()
    {
        QTest::addColumn<bool>("complete");
        QTest::newRow("rollback") << false;
        QTest::newRow("complete") << true;
    }

    void journalRecoveryRoundTrip()
    {
        QFETCH(bool, complete);
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto snapshot = LabelPlusDocument::serialize(project).toUtf8();
        FileRenameTransaction transaction({{project.images()[0].path, dir.filePath("new.png")}}, dir.path());
        transaction.setProjectCommit(project.filePath(), snapshot, snapshot);
        QVERIFY(transaction.apply());
        const auto details = ProjectRecoveryService::recoveryDetails(transaction.recoveryPath());
        QVERIFY(details);
        QVERIFY(details->canRecover);
        QCOMPARE(details->before, snapshot);
        QCOMPARE(details->moves[0].source, project.images()[0].path);
        const auto record = bytes(transaction.recoveryPath());
        transaction.commit();
        QVERIFY(AtomicFileWriter::write(transaction.recoveryPath(), record));
        QVERIFY(ProjectRecoveryService::recoverFiles(transaction.recoveryPath(), complete));
        QCOMPARE(bytes(complete ? dir.filePath("new.png") : project.images()[0].path), QByteArray("image"));
        QCOMPARE(bytes(project.filePath()), snapshot);
    }

    void journalInvalidSchemaAndSnapshot()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        const auto path = dir.filePath(".labelqt-rename-invalid.json");
        auto valid = journal(project, dir);
        QVector<QJsonObject> invalid;
        auto add = [&](const QString& field, const QJsonValue& value) {
            auto object = valid;
            object.insert(field, value);
            invalid.append(object);
        };
        add("version", 2.5);
        add("moves", QJsonArray{42});
        add("before", "%%%broken%%%");
        add("before", QString::fromLatin1(QByteArray(BoundedJsonIo::SnapshotBytes + 1, 'x').toBase64()));
        add("after", QString::fromLatin1(QByteArray("not a project").toBase64()));
        QJsonArray excessive;
        for (int i = 0; i <= BoundedJsonIo::RecordItems; ++i)
            excessive.append(valid.value("moves").toArray()[0]);
        add("moves", excessive);
        for (const auto& record : invalid) {
            const auto encoded = QJsonDocument(record).toJson();
            QVERIFY(AtomicFileWriter::write(path, encoded));
            QVERIFY(!ProjectRecoveryService::recoveryDetails(path));
            QVERIFY(!ProjectRecoveryService::recoverFiles(path, true));
            QCOMPARE(bytes(path), encoded);
            QCOMPARE(bytes(project.images()[0].path), QByteArray("image"));
            QVERIFY(!QFileInfo::exists(dir.filePath("new.png")));
        }
        auto legacy = valid;
        legacy.insert("version", 1);
        legacy.remove("project");
        legacy.remove("before");
        legacy.remove("after");
        QVERIFY(AtomicFileWriter::write(path, QJsonDocument(legacy).toJson()));
        const auto details = ProjectRecoveryService::recoveryDetails(path);
        QVERIFY(details);
        QVERIFY(!details->canRecover);
        QCOMPARE(details->moves.size(), 1);
        QVERIFY(!ProjectRecoveryService::recoverFiles(path, false));
        QCOMPARE(ProjectRecoveryService::entries(project.filePath(), AppPreferences{}).size(), 1);
    }

    void journalSnapshotBoundaryIsReadableAfterWrite()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        auto snapshot = LabelPlusDocument::serialize(project).toUtf8();
        snapshot += QByteArray(BoundedJsonIo::SnapshotBytes - snapshot.size(), ' ');
        QVERIFY(AtomicFileWriter::write(project.filePath(), snapshot));
        FileRenameTransaction transaction({{project.images()[0].path, dir.filePath("new.png")}}, dir.path());
        transaction.setProjectCommit(project.filePath(), snapshot, snapshot);
        QVERIFY(transaction.apply());
        const auto details = ProjectRecoveryService::recoveryDetails(transaction.recoveryPath());
        QVERIFY(details);
        QVERIFY(details->canRecover);
        QCOMPARE(details->before.size(), BoundedJsonIo::SnapshotBytes);
        QCOMPARE(details->after, snapshot);
        const auto record = bytes(transaction.recoveryPath());
        transaction.commit();
        QVERIFY(AtomicFileWriter::write(transaction.recoveryPath(), record));
        QVERIFY(ProjectRecoveryService::recoverFiles(transaction.recoveryPath(), false));
        QCOMPARE(bytes(project.filePath()), snapshot);
    }

    void backupBudgetsAndPreflight()
    {
        QTemporaryDir dir;
        auto project = fixture(dir);
        const auto path = dir.filePath("backup.txt");
        const auto recordPath = path + ".labelqt-backup.json";
        QVERIFY(AtomicFileWriter::write(recordPath, "existing evidence"));
        QVERIFY(!ProjectRecoveryService::writeBackup(project, path));
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(bytes(recordPath), QByteArray("existing evidence"));
        QVERIFY(QFile::remove(recordPath));
        QVERIFY(ProjectRecoveryService::writeBackup(project, path));
        auto values = AppPreferences{}.values();
        values.backupPath = dir.path();
        const auto records = ProjectRecoveryService::entries(project.filePath(), AppPreferences::fromValues(values));
        QCOMPARE(records.size(), 1);
        QVERIFY(ProjectRecoveryService::preview(records[0]));
        QVERIFY(ProjectRecoveryService::restoreCopy(records[0], dir.filePath("restored.txt")));
        project.setCommentLines({QString(BoundedJsonIo::SnapshotBytes + 1, QLatin1Char('x'))});
        const auto oversized = dir.filePath("oversized.txt");
        QVERIFY(!ProjectRecoveryService::writeBackup(project, oversized));
        QVERIFY(!QFileInfo::exists(oversized));
        QVERIFY(!QFileInfo::exists(oversized + ".labelqt-backup.json"));
        QFile snapshot(path);
        QVERIFY(snapshot.open(QIODevice::WriteOnly));
        QVERIFY(snapshot.resize(BoundedJsonIo::SnapshotBytes + 1));
        snapshot.close();
        QVERIFY(!ProjectRecoveryService::preview(records[0]));
        QFile index(recordPath);
        QVERIFY(index.open(QIODevice::WriteOnly));
        QVERIFY(index.resize(BoundedJsonIo::Backup.bytes + 1));
        index.close();
        QVERIFY(ProjectRecoveryService::entries(project.filePath(), AppPreferences::fromValues(values)).isEmpty());
    }

    void backupPreviewKeepsBomEncodingCompatibility()
    {
        QTemporaryDir dir;
        const auto project = fixture(dir);
        QStringEncoder encoder(QStringConverter::Utf16LE);
        const auto text = LabelPlusDocument::serialize(project);
        const auto encoded = QByteArray::fromHex("fffe") + encoder(text);
        const auto path = dir.filePath("previous.txt");
        QVERIFY(AtomicFileWriter::write(path, encoded));
        ProjectRecoveryEntry entry;
        entry.projectPath = project.filePath();
        entry.snapshotPath = path;
        entry.digest = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256);
        const auto preview = ProjectRecoveryService::preview(entry);
        QVERIFY(preview);
        QCOMPARE(preview->images()[0].labels[0].text(), project.images()[0].labels[0].text());
    }
};

QTEST_GUILESS_MAIN(RecordStorageTests)
#include "RecordStorageTests.moc"
