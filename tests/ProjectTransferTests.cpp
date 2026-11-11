#include "core/LabelPlusDocument.h"
#include "core/ProjectIdentityService.h"
#include "core/ProjectMetadataService.h"
#include "services/project/PageSourceInfoService.h"
#include "services/project/ProjectCreationService.h"
#include "services/proofread/ReviewRoundService.h"
#include "services/transfer/ProjectTransferService.h"
#include "services/transfer/TransferSupport.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <memory>
#include <utility>
#include <zlib.h>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
void put(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        qFatal("Cannot create fixture directory");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write fixture");
}

QByteArray get(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read fixture");
    return file.readAll();
}

QByteArray document(const QStringList& names = {"page.png"}, const QJsonObject& metadata = {})
{
    QString text = "1,0\n-\ndialogue\n-\n";
    text += ProjectMetadataService::rewriteCommentLines({}, metadata).join('\n') + '\n';
    for (const auto& name : names)
        text += QStringLiteral(">>>>>>>>[%1]<<<<<<<<\n").arg(name);
    return text.toUtf8();
}

struct Entry {
    QByteArray name;
    QByteArray bytes;
    quint32 mode{0100600};
    quint16 flags{0x0800};
    quint16 method{0};
};

QByteArray deflateRaw(const QByteArray& input)
{
    if (!std::in_range<uInt>(input.size()) || !std::in_range<uLong>(input.size()))
        qFatal("Fixture exceeds zlib input limits");
    z_stream stream{};
    if (deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        qFatal("Cannot create compressed fixture");
    const auto bound = deflateBound(&stream, static_cast<uLong>(input.size()));
    if (!std::in_range<qsizetype>(bound) || !std::in_range<uInt>(bound))
        qFatal("Fixture exceeds zlib output limits");
    QByteArray output(static_cast<qsizetype>(bound), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
        qFatal("Cannot compress fixture");
    if (stream.total_out > bound)
        qFatal("Compressed fixture exceeds its allocated buffer");
    output.resize(static_cast<qsizetype>(stream.total_out));
    deflateEnd(&stream);
    return output;
}

// Small ZIP fixture writer: permits malformed names/types which normal ZIP writers sanitize.
QByteArray zip(const QVector<Entry>& entries)
{
    QByteArray bytes, central;
    QDataStream local(&bytes, QIODevice::WriteOnly), index(&central, QIODevice::WriteOnly);
    local.setByteOrder(QDataStream::LittleEndian);
    index.setByteOrder(QDataStream::LittleEndian);
    for (const auto& entry : entries) {
        const auto offset = static_cast<quint32>(bytes.size());
        const auto size = static_cast<quint32>(entry.bytes.size());
        const auto payload = entry.method == 8 ? deflateRaw(entry.bytes) : entry.bytes;
        const auto compressed = static_cast<quint32>(payload.size());
        const auto crc = static_cast<quint32>(crc32(0, reinterpret_cast<const Bytef*>(entry.bytes.constData()), size));
        local << quint32(0x04034b50) << quint16(20) << entry.flags << entry.method << quint16(0) << quint16(0) << crc
              << compressed << size << quint16(entry.name.size()) << quint16(0);
        local.writeRawData(entry.name.constData(), entry.name.size());
        local.writeRawData(payload.constData(), payload.size());
        index << quint32(0x02014b50) << quint16(0x0314) << quint16(20) << entry.flags << entry.method << quint16(0)
              << quint16(0) << crc << compressed << size << quint16(entry.name.size()) << quint16(0) << quint16(0)
              << quint16(0) << quint16(0) << quint32(entry.mode << 16) << offset;
        index.writeRawData(entry.name.constData(), entry.name.size());
    }
    const auto offset = static_cast<quint32>(bytes.size());
    local.writeRawData(central.constData(), central.size());
    local << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size()) << quint16(entries.size())
          << quint32(central.size()) << offset << quint16(0);
    return bytes;
}

Project fixture(const QString& directory)
{
    Project project;
    project.setFilePath(QDir(directory).filePath("source.txt"));
    project.setGroups({"dialogue"});
    project.images() = {{"page.png", QDir(directory).filePath("page.png"), {Label("hello\n", "dialogue", {0.2, 0.3})}}};
    put(project.images().first().path, "original image bytes");
    LabelPlusDocument::saveToFile(project, project.filePath());
    return project;
}

QByteArray imageBytes()
{
    QImage image(32, 48, QImage::Format_RGB32);
    image.fill(Qt::white);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        qFatal("Cannot generate image fixture");
    return bytes;
}

void writeFixtureArchive(const QString& path, const QVector<Entry>& entries, bool sevenZip, bool gzip = false)
{
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
    const auto format =
        sevenZip ? archive_write_set_format_7zip(writer.get()) : archive_write_set_format_pax_restricted(writer.get());
    if (format != ARCHIVE_OK || (gzip && archive_write_add_filter_gzip(writer.get()) != ARCHIVE_OK) ||
        archive_write_set_bytes_per_block(writer.get(), 0) != ARCHIVE_OK ||
        archive_write_open_filename(writer.get(), QFile::encodeName(path).constData()) != ARCHIVE_OK)
        qFatal("Cannot open archive fixture");
    for (const auto& item : entries) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        archive_entry_set_pathname_utf8(entry.get(), item.name.constData());
        archive_entry_set_mode(entry.get(), item.mode);
        if ((item.mode & AE_IFMT) == AE_IFLNK)
            archive_entry_set_symlink(entry.get(), "../outside");
        archive_entry_set_size(entry.get(), item.bytes.size());
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK ||
            (!item.bytes.isEmpty() && archive_write_data(writer.get(), item.bytes.data(),
                                                         static_cast<size_t>(item.bytes.size())) != item.bytes.size()))
            qFatal("Cannot write archive fixture");
    }
    if (archive_write_close(writer.get()) != ARCHIVE_OK)
        qFatal("Cannot close archive fixture");
}

Project withLabelCounts(Project project, const QList<int>& counts)
{
    const auto imagePath = project.images().first().path;
    project.images().clear();
    for (qsizetype page = 0; page < counts.size(); ++page) {
        ImageEntry image;
        image.name = QStringLiteral("page%1.png").arg(page);
        image.path = imagePath;
        for (int label = 0; label < counts.at(page); ++label)
            image.labels.append(Label("text", "dialogue", {0.2, 0.3}));
        project.images().append(std::move(image));
    }
    return project;
}

Project withCurrentMetadata(Project project)
{
    // Use owner encoders, not hand-built approximations of the persisted schemas.
    project.setCommentLines(ReviewMetadataService::rewriteCommentLines(
        project.commentLines(), ReviewMetadataService::captureBaseline(project)));
    ReviewSession session;
    session.annotations.insert("change", {ReviewStatus::Reviewed, "checked"});
    project.setCommentLines(
        ReviewSessionService::withProjectSession(project.commentLines(), ReviewSessionService::encode(session)));
    PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(
        project, {{project.images().first().name, {1, "/private/origin.txt"}}});
    ReviewRound round;
    round.before = ReviewRoundService::snapshot(project);
    auto metadata = ProjectMetadataService::metadataObject(project.commentLines());
    metadata.insert("proofreading", ReviewRoundService::encode(round, project.filePath()));
    project.setCommentLines(ProjectMetadataService::rewriteCommentLines(project.commentLines(), metadata));
    return LabelPlusDocument::parse(LabelPlusDocument::serialize(project), project.filePath());
}

void noPublication(const ProjectTransferService& service, const QString& root)
{
    QCOMPARE(service.listWorkspaces().size(), 0);
    QCOMPARE(QDir(QDir(root).filePath("workspaces")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    QCOMPARE(QDir(QDir(root).filePath("outgoing")).entryList(QDir::Files).size(), 0);
    QCOMPARE(QDir(root).entryList({".staging-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
}
} // namespace

class ProjectTransferTests final : public QObject {
    Q_OBJECT
private slots:
    void commentFreeExportPreservesLocalStateAndOnlyCopiesLiveImages()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project.setSourceName("original project.txt");
        project.images()[0].labels[0].setText("Live text\nsecond line");
        ReviewRound round;
        round.before = ReviewRoundService::snapshot(project);
        round.before.images()[0].path = directory.filePath("source/missing-history.png");
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines(
            {"Private comment"}, {{"proofreading", ReviewRoundService::encode(round, project.filePath())}}));
        const auto original = LabelPlusDocument::serialize(project);
        const auto diskBytes = get(project.filePath());
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(!service.exportZip(project)); // Full export still requires its historical images.
        const auto exported = service.exportZip(project, "clean.zip", LabelPlusDocument::CommentPolicy::Omit);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString{} : exported.error()));
        const auto extracted = directory.filePath("extracted");
        QVERIFY(QDir().mkpath(extracted));
        auto files = transfer::extractZip(*exported, extracted, {});
        files.sort();
        QCOMPARE(files, QStringList({"project/page.png", "project/project.txt"}));
        const auto path = QDir(extracted).filePath("project/project.txt");
        const auto text = get(path);
        QVERIFY(text.startsWith("1,0\n-\ndialogue\n-\n\n>>>>>>>>[page.png]<<<<<<<<\n"));
        QVERIFY(!text.contains("LabelQt"));
        QVERIFY(!text.contains("Private comment"));
        const auto loaded = LabelPlusDocument::loadFromFile(path);
        QVERIFY(loaded.commentLines().isEmpty());
        QVERIFY(loaded.sourceName().isEmpty());
        QCOMPARE(loaded.images()[0].labels[0].text(), project.images()[0].labels[0].text());
        QCOMPARE(loaded.images()[0].labels[0].group(), project.images()[0].labels[0].group());
        QCOMPARE(loaded.images()[0].labels[0].position(), project.images()[0].labels[0].position());
        QVERIFY(loaded.id() != project.id());
        QCOMPARE(LabelPlusDocument::serialize(project), original);
        QCOMPARE(get(project.filePath()), diskBytes);
        // Clearing comments must never disable the current-image path boundary.
        project.images()[0].path = directory.filePath("outside.png");
        QVERIFY(!service.exportZip(project, "unsafe.zip", LabelPlusDocument::CommentPolicy::Omit));
    }

    void macMetadataIsNotProjectContent()
    {
        QTemporaryDir directory;
        const auto input = directory.filePath("pages.zip");
        for (bool existingProject : {false, true}) {
            QVector<Entry> entries{{"book/page.png", imageBytes()},
                                   {"__MACOSX/book/._page.png", "resource fork"},
                                   {"book/._page.png", "resource fork"},
                                   {"__MACOSX/book/._work.txt", "resource fork"},
                                   {"book/.DS_Store", "finder metadata"}};
            if (existingProject)
                entries.append({"book/work.txt", document()});
            put(input, zip(entries));
            const auto imported = ProjectTransferService(directory.filePath("app")).importFile(input);
            QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
            const auto project = LabelPlusDocument::loadFromFile(imported->projectPath);
            QCOMPARE(project.images().size(), 1);
            QVERIFY(QFileInfo::exists(project.images().first().path));
            QVERIFY(project.images().first().name.endsWith("page.png"));
        }
        // Ignoring metadata for page discovery must not bypass archive security checks.
        put(input, zip({{"page.png", imageBytes()}, {"__MACOSX/../escape.png", "unsafe"}}));
        ProjectTransferService unsafeService(directory.filePath("unsafe"));
        QVERIFY(!unsafeService.importFile(input));
        noPublication(unsafeService, directory.filePath("unsafe"));
    }

    void gzipIntegrityIsChecked_data()
    {
        QTest::addColumn<int>("extraBytes");
        QTest::newRow("small") << 0;
        QTest::newRow("multiple-read-buffers") << 200000;
    }

    void gzipIntegrityIsChecked()
    {
        QFETCH(int, extraBytes);
        QTemporaryDir directory;
        const auto input = directory.filePath("pages.tgz");
        QByteArray payload(extraBytes, Qt::Uninitialized);
        QRandomGenerator random(12345);
        for (auto& byte : payload)
            byte = static_cast<char>(random.generate() & 0xff);
        writeFixtureArchive(input, {{"page.png", imageBytes()}, {"extra.bin", payload}}, false, true);
        const auto valid = get(input);
        for (int corruption = 0; corruption < 5; ++corruption) {
            auto bytes = valid;
            if (corruption == 0)
                bytes[bytes.size() - 8] ^= 1; // Payload CRC.
            else if (corruption == 1)
                bytes[bytes.size() - 4] ^= 1; // Uncompressed size.
            else if (corruption == 2)
                bytes.chop(8); // Missing trailer.
            else if (corruption == 3)
                bytes.append("unexpected trailing bytes");
            else
                bytes.append(valid); // A second gzip member must not be silently ignored by tar.
            put(input, bytes);
            ProjectTransferService service(directory.filePath("app"));
            QVERIFY2(!service.importFile(input), qPrintable(QString::number(corruption)));
            noPublication(service, directory.filePath("app"));
        }
        put(input, valid + QByteArray(1024, '\0'));
        const auto padded = ProjectTransferService(directory.filePath("app")).importFile(input);
        QVERIFY2(padded.has_value(), qPrintable(padded ? QString{} : padded.error()));
    }

    void rarReadersAndVolumeGate()
    {
        const auto fixtureDir = QStringLiteral(LABELQT_SOURCE_DIR "/tests/fixtures/archives/");
        for (const auto& version : {QStringLiteral("rar4"), QStringLiteral("rar5")}) {
            auto bytes = QByteArray::fromBase64(get(fixtureDir + version + ".base64"));
            const bool rar5 = version == "rar5";
            // Keep only the first regular file and the end marker from the RAR4 link fixture.
            if (!rar5)
                bytes = bytes.first(90) + bytes.last(7);
            QTemporaryDir directory;
            const auto input = directory.filePath("renamed.zip");
            put(input, bytes);
            QFile probe(input);
            QVERIFY(probe.open(QIODevice::ReadOnly));
            const auto format = transfer::detectArchive(probe);
            QCOMPARE(format, rar5 ? transfer::ArchiveFormat::Rar5 : transfer::ArchiveFormat::Rar);
            probe.close();
            const auto extracted = directory.filePath("extracted");
            QVERIFY(QDir().mkpath(extracted));
            const auto files = transfer::extractArchive(input, extracted, *format, {});
            QCOMPARE(files, QStringList{rar5 ? "helloworld.txt" : "test.txt"});
            QCOMPARE(get(QDir(extracted).filePath(files.first())),
                     rar5 ? QByteArray("hello libarchive test suite!\n") : QByteArray("test text document\r\n"));
            // These flags describe a volume even when its first file is complete.
            if (rar5) {
                bytes[16] = char(static_cast<quint8>(bytes[16]) | 1);
                qToLittleEndian<quint32>(
                    static_cast<quint32>(crc32(0, reinterpret_cast<const Bytef*>(bytes.constData() + 12), 11)),
                    bytes.data() + 8);
            }
            else {
                bytes[10] = char(static_cast<quint8>(bytes[10]) | 1);
                qToLittleEndian<quint16>(
                    static_cast<quint16>(crc32(0, reinterpret_cast<const Bytef*>(bytes.constData() + 9), 11)),
                    bytes.data() + 7);
            }
            put(input, bytes);
            QVERIFY_EXCEPTION_THROWN(transfer::extractArchive(input, directory.filePath("volume"), *format, {}),
                                     std::runtime_error);
        }
    }

    void archiveCreationFailureIsAtomic()
    {
        QTemporaryDir directory;
        const auto input = directory.filePath("input.7z");
        ProjectTransferService service(directory.filePath("app"));
        writeFixtureArchive(input, {{"page.png", "broken image"}}, true);
        const auto broken = service.importFile(input);
        QVERIFY(!broken);
        QVERIFY(broken.error().contains("Cannot read an image"));
        writeFixtureArchive(input, {{"work.txt", "broken project"}, {"page.png", imageBytes()}}, true);
        QVERIFY(!service.importFile(input)); // A malformed TXT must not silently become a new image project.
        noPublication(service, directory.filePath("app"));
        writeFixtureArchive(input, {{"page.png", imageBytes()}, {"extra.png", imageBytes()}}, true);
        const auto valid = get(input);
        for (int option = 0; option < 3; ++option) {
            TransferLimits limits;
            if (option == 0)
                limits.fileBytes = 16;
            if (option == 1)
                limits.totalBytes = 16;
            if (option == 2)
                limits.entries = 1;
            QVERIFY(!ProjectTransferService(directory.filePath("app"), limits).importFile(input));
        }
        put(input, valid.first(valid.size() / 2));
        QVERIFY(!service.importFile(input));
        noPublication(service, directory.filePath("app"));
    }

    void tarRootDirectoryIsNotAnUnsafePage()
    {
        QTemporaryDir directory;
        const auto input = directory.filePath("pages.tgz");
        writeFixtureArchive(input, {{"./", {}, 0040700}, {"./pages/", {}, 0040700}, {"./pages/2.png", imageBytes()}},
                            false, true);
        const auto imported = ProjectTransferService(directory.filePath("app")).importFile(input);
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
        QCOMPARE(LabelPlusDocument::loadFromFile(imported->projectPath).images().first().name, "pages/2.png");
    }

    void imageArchivesCreateIndependentProjects_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto& kind : {"zip", "7z", "tar", "tar.gz"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void imageArchivesCreateIndependentProjects()
    {
        QFETCH(QString, kind);
        QTemporaryDir directory;
        const auto input = directory.filePath("transport.zip"); // Extension is deliberately wrong for non-ZIP.
        const auto pixels = imageBytes();
        const QVector<Entry> entries{{"book/10.png", pixels},
                                     {"book/2.png", pixels},
                                     {"book/1.png", pixels},
                                     {"other/1.png", pixels},
                                     {"ignored.json", "{}"}};
        if (kind == "zip")
            put(input, zip(entries));
        else
            writeFixtureArchive(input, entries, kind == "7z", kind == "tar.gz");
        const auto original = get(input);
        ProjectTransferService service(directory.filePath("app"));
        const auto first = service.importFile(input, false, "chapter." + kind);
        QVERIFY2(first.has_value(), qPrintable(first ? QString{} : first.error()));
        QCOMPARE(first->title, "chapter");
        QCOMPARE(first->archiveName, "chapter.zip");
        const auto project = LabelPlusDocument::loadFromFile(first->projectPath);
        QCOMPARE(project.groups(), ProjectCreationService::defaultGroups());
        QCOMPARE(project.images().size(), 4);
        QCOMPARE(project.images()[0].name, "book/1.png");
        QCOMPARE(project.images()[1].name, "book/2.png");
        QCOMPARE(project.images()[2].name, "book/10.png");
        QCOMPARE(project.images()[3].name, "other/1.png");
        for (const auto& page : project.images()) {
            QVERIFY(page.labels.isEmpty());
            QVERIFY(!page.id.isNull());
            QCOMPARE(get(page.path), pixels);
        }
        QVERIFY(!QFileInfo::exists(QFileInfo(first->projectPath).dir().filePath("ignored.json")));
        const auto second = service.importFile(input, false, "chapter." + kind);
        QVERIFY(second.has_value());
        const auto another = LabelPlusDocument::loadFromFile(second->projectPath);
        QVERIFY(second->id != first->id);
        QVERIFY(another.id() != project.id());
        QVERIFY(another.images()[0].id != project.images()[0].id);
        const auto exported = service.exportZip(project);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString{} : exported.error()));
        const auto roundtrip = service.importFile(*exported);
        QVERIFY(roundtrip.has_value());
        QCOMPARE(LabelPlusDocument::loadFromFile(roundtrip->projectPath).images()[0].id, project.images()[0].id);
        QCOMPARE(get(input), original);
    }

    void otherArchivesPreserveExistingProject_data()
    {
        imageArchivesCreateIndependentProjects_data();
    }

    void otherArchivesPreserveExistingProject()
    {
        QFETCH(QString, kind);
        QTemporaryDir directory;
        const auto source = fixture(directory.filePath("original"));
        const QVector<Entry> entries{{"book/work.txt", get(source.filePath())},
                                     {"book/page.png", get(source.images()[0].path)},
                                     {"unused.png", imageBytes()}};
        const auto input = directory.filePath("renamed.zip");
        if (kind == "zip")
            put(input, zip(entries));
        else
            writeFixtureArchive(input, entries, kind == "7z", kind == "tar.gz");
        const auto result = ProjectTransferService(directory.filePath("app")).importFile(input);
        QVERIFY2(result.has_value(), qPrintable(result ? QString{} : result.error()));
        const auto project = LabelPlusDocument::loadFromFile(result->projectPath);
        QCOMPARE(project.images().size(), 1);
        QCOMPARE(project.images()[0].labels[0].text(), "hello\n");
        QCOMPARE(project.id(), source.id());
    }

    void otherArchivesRejectUnsafeMembers_data()
    {
        QTest::addColumn<bool>("sevenZip");
        QTest::addColumn<QByteArray>("name");
        QTest::addColumn<bool>("link");
        for (bool sevenZip : {false, true}) {
            const auto prefix = sevenZip ? QByteArray("7z-") : QByteArray("tar-");
            QTest::newRow((prefix + "traversal").constData()) << sevenZip << QByteArray("../outside") << false;
            QTest::newRow((prefix + "duplicate").constData()) << sevenZip << QByteArray("page.png") << false;
            QTest::newRow((prefix + "case").constData()) << sevenZip << QByteArray("PAGE.png") << false;
            QTest::newRow((prefix + "link").constData()) << sevenZip << QByteArray("link") << true;
        }
    }

    void otherArchivesRejectUnsafeMembers()
    {
        QFETCH(bool, sevenZip);
        QFETCH(QByteArray, name);
        QFETCH(bool, link);
        QTemporaryDir directory;
        const auto input = directory.filePath("input.zip");
        writeFixtureArchive(input, {{"page.png", imageBytes()}, {name, {}, link ? 0120777U : 0100600U}}, sevenZip);
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(!service.importFile(input));
        noPublication(service, directory.filePath("app"));
        QVERIFY(!QFileInfo::exists(directory.filePath("outside")));
    }

    void naturalImageOrderIsDeterministic()
    {
        QStringList names{"p10.png",
                          "p2.png",
                          "p01.png",
                          "p1.png",
                          "p000.png",
                          "p0.png",
                          "p99999999999999999999.png",
                          "p100000000000000000000.png"};
        ProjectCreationService::sortImageNames(names);
        QCOMPARE(names, (QStringList{"p0.png", "p000.png", "p01.png", "p1.png", "p2.png", "p10.png",
                                     "p99999999999999999999.png", "p100000000000000000000.png"}));
    }

    void optionalLocalArchiveRegression()
    {
        const auto path = qEnvironmentVariable("LABELQT_TEST_IMPORT_ARCHIVE");
        if (path.isEmpty())
            QSKIP("Set LABELQT_TEST_IMPORT_ARCHIVE to check a private sample without committing it.");
        QTemporaryDir directory;
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.importFile(path);
        QVERIFY2(result.has_value(), qPrintable(result ? QString{} : result.error()));
        const auto project = LabelPlusDocument::loadFromFile(result->projectPath);
        QVERIFY(!project.images().isEmpty());
        const auto exported = service.exportZip(project);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString{} : exported.error()));
        qInfo() << "Imported pages:" << project.images().size() << "; re-export verified";
    }

    void workspaceManagementAndRetention()
    {
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        const auto root = directory.filePath("app");
        ProjectTransferService transfer(root);
        WorkspaceStorageService storage(root);
        const auto imported = transfer.importFile(project.filePath());
        QVERIFY(imported);
        const auto original = get(imported->projectPath);
        const auto renamed = storage.rename(imported->id, "My project");
        QVERIFY(renamed);
        QCOMPARE(renamed->title, "My project");
        QCOMPARE(renamed->archiveName, imported->archiveName);
        QCOMPARE(renamed->modifiedAt, imported->modifiedAt);
        QCOMPARE(get(imported->projectPath), original);
        QVERIFY(!storage.rename(imported->id, "   "));
        QVERIFY(!storage.remove("../source"));
        const auto first = transfer.exportZip(project, "chapter revised.zip");
        const auto second = transfer.exportZip(project, "chapter revised.zip");
        QVERIFY(first && second);
        QVERIFY(*first != *second);
        QCOMPARE(QFileInfo(*first).fileName(), "chapter revised.zip");
        const auto exportedBytes = get(*first);
        const auto stage = QDir(root).filePath(".staging-export-Ab1234");
        put(stage + "/unfinished.zip", "partial");
        const auto freshStage = QDir(root).filePath(".staging-import-Zz1234");
        put(freshStage + "/input.zip", "partial");
        const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 8);
        std::filesystem::last_write_time(stage.toStdString(), old);
        std::filesystem::last_write_time(QFileInfo(*first).absolutePath().toStdString(), old);
        const auto legacy =
            QDir(root).filePath("outgoing/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".zip");
        put(legacy, "legacy export");
        std::filesystem::last_write_time(legacy.toStdString(), old);
        QVERIFY(storage.cleanDisposableFiles());
        QVERIFY(!QFileInfo::exists(stage));
        QVERIFY(!QFileInfo::exists(*first));
        QVERIFY(!QFileInfo::exists(legacy));
        QVERIFY(QFileInfo::exists(freshStage));
        QCOMPARE(get(*second), exportedBytes);
        QCOMPARE(get(imported->projectPath), original);
        // Directory deletion must not follow a link into unrelated storage.
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(QFileInfo(project.filePath()).absolutePath(),
                            QFileInfo(imported->projectPath).absolutePath() + "/outside"));
#endif
        QVERIFY(storage.remove(imported->id));
        QVERIFY(!QFileInfo::exists(imported->projectPath));
        QCOMPARE(transfer.listWorkspaces().size(), 0);
        QVERIFY(storage.cleanDisposableFiles());
        QVERIFY(QFileInfo::exists(project.filePath()));
        QVERIFY(QFileInfo::exists(*second));
        QCOMPARE(QDir(root).entryList({".deleted-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
    }

    void shareNamesAndCacheQuota()
    {
        for (const auto& name : {QString{}, QString("../x"), QString("a/b"), QString("a\\b"), QString("a*b"),
                                 QString("CON"), QString("a:b"), QString(".zip"), QString(256, 'a')})
            QVERIFY2(!WorkspaceStorageService::archiveFileName(name), qPrintable(name));
        QCOMPARE(*WorkspaceStorageService::archiveFileName("chapter"), "chapter.zip");
        QCOMPARE(*WorkspaceStorageService::archiveFileName(" chapter.ZIP "), "chapter.ZIP");
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        const auto root = directory.filePath("app");
        ProjectTransferService transfer(root);
        QVERIFY(transfer.importFile(project.filePath()));
        const auto full = QDir(root).filePath("outgoing/reserved.zip");
        put(full, {});
        QFile sparse(full);
        QVERIFY(sparse.open(QIODevice::WriteOnly));
        QVERIFY(sparse.resize(WorkspaceStorageService::shareCacheLimit));
        sparse.close();
        const auto result = transfer.exportZip(project, "new.zip");
        QVERIFY(!result);
        QVERIFY(result.error().contains("2 GiB"));
        QCOMPARE(QDir(QDir(root).filePath("outgoing")).entryList(QDir::Files), QStringList{"reserved.zip"});
        QCOMPARE(QDir(root).entryList({".staging-*"}, QDir::Dirs | QDir::Hidden).size(), 0);
        QCOMPARE(transfer.listWorkspaces().size(), 1);
    }

    void oldWorkspaceManifestStillLoads()
    {
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        ProjectTransferService transfer(directory.filePath("app"));
        const auto imported = transfer.importFile(project.filePath());
        QVERIFY(imported);
        const auto path = QFileInfo(QFileInfo(imported->projectPath).absolutePath()).absolutePath() + "/workspace.json";
        auto metadata = QJsonDocument::fromJson(get(path)).object();
        metadata.remove("archiveName");
        put(path, QJsonDocument(metadata).toJson());
        const auto entries = transfer.listWorkspaces();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().archiveName, imported->title + ".zip");
    }

    void txtCreatesIndependentDiscoverableCopies()
    {
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        const auto original = get(project.filePath());
        const auto root = directory.filePath("app");
        ProjectTransferService service(root);
        const auto first = service.importFile(project.filePath());
        QVERIFY2(first.has_value(), qPrintable(first ? QString() : first.error()));
        const auto second = service.importFile(project.filePath());
        QVERIFY(second.has_value());
        QVERIFY(first->id != second->id);
        QCOMPARE(first->title, "source");
        QCOMPARE(ProjectTransferService(root).listWorkspaces().size(), 2);
        auto loaded = LabelPlusDocument::loadFromFile(first->projectPath);
        QCOMPARE(loaded.id(), project.id());
        QCOMPARE(loaded.images().first().id, project.images().first().id);
        QCOMPARE(loaded.images().first().labels.first().text(), "hello\n");
        QCOMPARE(get(loaded.images().first().path), "original image bytes");
        put(loaded.images().first().path, "edited copy");
        QCOMPARE(get(project.images().first().path), "original image bytes");
        QCOMPARE(get(project.filePath()), original);
    }

    void txtOnlyAndMissingImages()
    {
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        ProjectTransferService service(directory.filePath("app"));
        const auto textOnly = service.importFile(project.filePath(), false);
        QVERIFY(textOnly.has_value());
        const auto loaded = LabelPlusDocument::loadFromFile(textOnly->projectPath);
        QVERIFY(!QFileInfo::exists(loaded.images().first().path));
        QVERIFY(!service.exportZip(loaded));
        QVERIFY(QFile::remove(project.images().first().path));
        QVERIFY(service.importFile(project.filePath()).has_value());
        put(directory.filePath("empty.txt"), document({}));
        const auto empty = service.importFile(directory.filePath("empty.txt"));
        QVERIFY(empty.has_value());
        QVERIFY(service.exportZip(LabelPlusDocument::loadFromFile(empty->projectPath)).has_value());
    }

    void zipNestedProjectDropsUnreferencedFiles()
    {
        QTemporaryDir directory;
        const auto input = directory.filePath("download.bin");
        const auto data = zip({{"book/", {}, 0040700},
                               {"book/work.txt", document({"images/page.png"})},
                               {"book/images/page.png", "pixels"},
                               {"secret.json", "secret"},
                               {"script.py", "do not run"}});
        put(input, data);
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.importFile(input);
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        const auto project = LabelPlusDocument::loadFromFile(result->projectPath);
        QCOMPARE(get(project.images().first().path), "pixels");
        QVERIFY(!QFileInfo::exists(QDir(QFileInfo(result->projectPath).absolutePath()).filePath("secret.json")));
        QCOMPARE(get(input), data);
    }

    void multipleOrNoTxtIsExplicit()
    {
        QTemporaryDir directory;
        ProjectTransferService service(directory.filePath("app"));
        const auto input = directory.filePath("input.zip");
        put(input, zip({{"one.txt", document()}, {"two.TXT", document()}}));
        const auto multiple = service.importFile(input);
        QVERIFY(!multiple);
        QVERIFY(multiple.error().contains("multiple TXT"));
        put(input, zip({{"unknown.bin", "pixels"}}));
        const auto none = service.importFile(input);
        QVERIFY(!none);
        QVERIFY(none.error().contains("neither a project TXT"));
        noPublication(service, directory.filePath("app"));
    }

    void unsafeArchive_data()
    {
        QTest::addColumn<QByteArray>("payload");
        const auto add = [](const char* tag, const QByteArray& name) {
            QTest::newRow(tag) << zip({{"work.txt", document()}, {name, "bad"}});
        };
        add("traversal", "../outside");
        add("absolute", "/outside");
        add("drive", "C:/outside");
        add("unc", "//server/share");
        add("backslash", "dir\\..\\outside");
        add("nul", QByteArray("page\0.png", 9));
        add("invalid-utf8", QByteArray::fromHex("ff2e706e67"));
        add("incomplete-utf8", QByteArray("page.png") + QByteArray::fromHex("c3"));
        add("trailing-dot", "page.");
        add("device", "aux.png");
        add("nested-extension", "other.zip");
        QTest::newRow("duplicate") << zip({{"work.txt", document()}, {"work.txt", document()}});
        QTest::newRow("case") << zip({{"work.txt", document()}, {"PAGE.png", "a"}, {"page.png", "b"}});
        QTest::newRow("unicode") << zip({{"work.txt", document()},
                                         {QByteArray::fromHex("c3a92e706e67"), "a"},
                                         {QByteArray::fromHex("65cc812e706e67"), "b"}});
        QTest::newRow("parent-case") << zip({{"work.txt", document()}, {"Dir/a", "a"}, {"dir/b", "b"}});
        QTest::newRow("file-directory") << zip({{"work.txt", document()}, {"dir", "a"}, {"dir/page", "b"}});
        QTest::newRow("duplicate-directory")
            << zip({{"work.txt", document()}, {"dir/", {}, 0040700}, {"dir/", {}, 0040700}});
        QTest::newRow("symlink") << zip({{"work.txt", document()}, {"page.png", "/etc/passwd", 0120777}});
        QTest::newRow("device-entry") << zip({{"work.txt", document()}, {"page.png", {}, 0020600}});
        QTest::newRow("encrypted") << zip({{"work.txt", document(), 0100600, 0x0801}});
        QTest::newRow("unsupported-method") << zip({{"work.txt", document(), 0100600, 0x0800, 99}});
        QTest::newRow("nested-magic") << zip({{"work.txt", document()}, {"page.png", zip({})}});
        auto corrupt = zip({{"work.txt", document()}});
        corrupt[30 + QByteArray("work.txt").size()] = '9';
        QTest::newRow("crc") << corrupt;
        auto truncated = zip({{"work.txt", document()}});
        truncated.chop(12);
        QTest::newRow("truncated") << truncated;
        auto split = zip({{"work.txt", document()}});
        split[split.size() - 18] = 1;
        QTest::newRow("split-volume") << split;
        auto zip64 = zip({{"work.txt", document()}});
        zip64[zip64.size() - 12] = char(0xff);
        zip64[zip64.size() - 11] = char(0xff);
        QTest::newRow("zip64") << zip64;
    }

    void unsafeArchive()
    {
        QFETCH(QByteArray, payload);
        QTemporaryDir directory;
        const auto root = directory.filePath("app");
        ProjectTransferService service(root);
        put(directory.filePath("input.zip"), payload);
        const auto result = service.importFile(directory.filePath("input.zip"));
        QVERIFY(!result);
        QVERIFY(!result.error().isEmpty());
        noPublication(service, root);
        QVERIFY(!QFileInfo::exists(directory.filePath("outside")));
    }

    void unsafeReferences_data()
    {
        QTest::addColumn<QString>("reference");
        QTest::newRow("parent") << "../outside.png";
        QTest::newRow("absolute") << "/etc/passwd";
        QTest::newRow("uri") << "content://provider/secret";
        QTest::newRow("url") << "https://host/secret";
        QTest::newRow("windows") << "C:\\secret.png";
        QTest::newRow("mixed") << "folder/..\\secret.png";
        QTest::newRow("normalized-parent") << "folder/../page.png";
    }

    void unsafeReferences()
    {
        QFETCH(QString, reference);
        QTemporaryDir directory;
        ProjectTransferService service(directory.filePath("app"));
        const auto path = directory.filePath("work.txt");
        put(path, document({reference}));
        QVERIFY(!service.importFile(path));
        const QJsonObject paths{{"version", 1}, {"paths", QJsonObject{{"page.png", reference}}}};
        put(path, document({"page.png"}, {{"imagePaths", paths}}));
        QVERIFY(!service.importFile(path));
        // Stale keys must be checked too, before the serializer can discard them.
        put(path,
            document({"page.png"},
                     {{"imagePaths", QJsonObject{{"version", 1}, {"paths", QJsonObject{{"unused.png", reference}}}}}}));
        QVERIFY(!service.importFile(path, false));
        noPublication(service, directory.filePath("app"));
    }

    void localLinksAreRejected()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(QFile::link(project.filePath(), directory.filePath("linked.txt")));
        QVERIFY(!service.importFile(directory.filePath("linked.txt")));
        QVERIFY(QFile::remove(project.images().first().path));
        put(directory.filePath("external.png"), "private");
        QVERIFY(QFile::link(directory.filePath("external.png"), project.images().first().path));
        QVERIFY(!service.importFile(project.filePath()));
        QVERIFY(!service.exportZip(project));
        QVERIFY(QFile::link(directory.filePath("source"), directory.filePath("linked-dir")));
        QVERIFY(!service.importFile(directory.filePath("linked-dir/source.txt")));
        QVERIFY(QFile::link(directory.filePath("source"), directory.filePath("linked-root")));
        QVERIFY(!ProjectTransferService(directory.filePath("linked-root")).importFile(project.filePath()));
        noPublication(service, directory.filePath("app"));
    }

    void zipDisplayNameKeepsOnlyStem_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("stem");
        QTest::newRow("plain") << QStringLiteral("chapter.zip") << QStringLiteral("chapter");
        QTest::newRow("multiple-dots") << QStringLiteral("chapter.03.review.zip")
                                       << QStringLiteral("chapter.03.review");
        QTest::newRow("uppercase") << QStringLiteral("chapter.ZIP") << QStringLiteral("chapter");
        QTest::newRow("chinese") << QStringLiteral("译稿.zip") << QStringLiteral("译稿");
        QTest::newRow("japanese") << QStringLiteral("第01話 校正.zip") << QStringLiteral("第01話 校正");
        QTest::newRow("supplementary-unicode")
            << QString::fromUtf8("\xF0\x9F\x93\x96.zip") << QString::fromUtf8("\xF0\x9F\x93\x96");
        QTest::newRow("stem-ends-in-dot") << QStringLiteral("chapter..zip") << QStringLiteral("chapter.");
    }

    void zipDisplayNameKeepsOnlyStem()
    {
        QFETCH(QString, name);
        QFETCH(QString, stem);
        QTemporaryDir directory;
        const auto input = directory.filePath("transport-id.zip");
        put(input, zip({{"translation.txt", document()}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.importFile(input, false, name);
        QVERIFY(result);
        QCOMPARE(result->title, stem);
        QCOMPARE(result->archiveName, name);
        const auto listed = service.listWorkspaces();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().title, stem);
    }

    void zipNameIsWorkspaceTitle()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("chapter-03.review.zip");
        put(path, zip({{"translation.txt", document()}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto local = service.importFile(path);
        QVERIFY(local.has_value());
        QCOMPARE(local->title, QStringLiteral("chapter-03.review"));
        const auto received = service.importFile(path, false, QStringLiteral("Another chapter.zip"));
        QVERIFY(received.has_value());
        QCOMPARE(received->title, QStringLiteral("Another chapter"));
        QCOMPARE(service.listWorkspaces().size(), 2);
        const auto invalid = service.importFile(path, false, QString(10000, QChar('x')) + ".zip");
        QVERIFY(!invalid.has_value());
        QCOMPARE(service.listWorkspaces().size(), 2);
    }

    void canonicalPrivateRootSupportsSystemAlias()
    {
#ifndef Q_OS_UNIX
        QSKIP("Requires POSIX directory symlinks.");
#else
        QTemporaryDir directory;
        const auto realRoot = directory.filePath("real/app/files");
        QVERIFY(QDir().mkpath(realRoot + "/inbox"));
        QVERIFY(QFile::link(directory.filePath("real"), directory.filePath("alias")));
        const auto systemRoot = directory.filePath("alias/app/files");
        const auto canonicalRoot = QFileInfo(systemRoot).canonicalFilePath();
        QCOMPARE(canonicalRoot, QFileInfo(realRoot).canonicalFilePath());

        // Java stages into getFilesDir().getCanonicalFile(); C++ must use the same root.
        const auto received = QDir(canonicalRoot).filePath("inbox/input.zip");
        put(received, zip({{"work.txt", document()}, {"page.png", "pixels"}}));
        QVERIFY(!ProjectTransferService(systemRoot).importFile(received, false));
        ProjectTransferService service(canonicalRoot);
        const auto imported = service.importFile(received, false);
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
        QCOMPARE(service.listWorkspaces().size(), 1);
        const auto project = LabelPlusDocument::loadFromFile(imported->projectPath);
        const auto exported = service.exportZip(project);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString{} : exported.error()));
        QVERIFY(QFile::exists(received));
        QCOMPARE(QFileInfo(QFileInfo(*exported).absolutePath()).absolutePath(),
                 QDir(canonicalRoot).filePath("outgoing"));

        // Canonicalizing the trusted root must not exempt files or subdirectories beneath it.
        const auto image = project.images().first().path;
        QVERIFY(QFile::remove(image));
        const auto external = directory.filePath("external.png");
        put(external, "private");
        QVERIFY(QFile::link(external, image));
        QVERIFY(!service.exportZip(project));
        QVERIFY(QFile::link(received, QDir(canonicalRoot).filePath("inbox/link.zip")));
        QVERIFY(!service.importFile(QDir(canonicalRoot).filePath("inbox/link.zip"), false));
#endif
    }

    void limitsAreEnforced_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("input") << 0;
        QTest::newRow("file") << 1;
        QTest::newRow("total") << 2;
        QTest::newRow("entries") << 3;
        QTest::newRow("depth") << 4;
        QTest::newRow("text") << 5;
    }

    void limitsAreEnforced()
    {
        QFETCH(int, kind);
        QTemporaryDir directory;
        TransferLimits limits;
        if (kind == 0)
            limits.inputBytes = 20;
        if (kind == 1)
            limits.fileBytes = 20;
        if (kind == 2)
            limits.totalBytes = 20;
        if (kind == 3)
            limits.entries = 1;
        if (kind == 4)
            limits.depth = 1;
        if (kind == 5)
            limits.textBytes = 10;
        ProjectTransferService service(directory.filePath("app"), limits);
        put(directory.filePath("input.zip"),
            zip({{"book/work.txt", document()}, {"book/page.png", QByteArray(1024, 'x')}}));
        QVERIFY(!service.importFile(directory.filePath("input.zip")));
        noPublication(service, directory.filePath("app"));
    }

    void labelCountLimits_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QList<int>>("counts");
        QTest::addColumn<bool>("accepted");
        for (const auto& target : {"live", "before", "after", "legacy"}) {
            QTest::newRow(qPrintable(QStringLiteral("%1-boundary").arg(target)))
                << QString::fromLatin1(target) << QList<int>{2, 1} << true;
            QTest::newRow(qPrintable(QStringLiteral("%1-per-page").arg(target)))
                << QString::fromLatin1(target) << QList<int>{3} << false;
            QTest::newRow(qPrintable(QStringLiteral("%1-total").arg(target)))
                << QString::fromLatin1(target) << QList<int>{2, 2} << false;
        }
    }

    void labelCountLimits()
    {
        QFETCH(QString, target);
        QFETCH(QList<int>, counts);
        QFETCH(bool, accepted);
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        const auto counted = withLabelCounts(project, counts);
        if (target == "live")
            project = counted;
        else if (target == "legacy")
            project.setCommentLines(
                ReviewMetadataService::rewriteCommentLines({}, ReviewMetadataService::captureBaseline(counted)));
        else {
            ReviewRound round;
            round.before = ReviewRoundService::snapshot(target == "before" ? counted : project);
            if (target == "after")
                round.after = ReviewRoundService::snapshot(counted);
            project.setCommentLines(ProjectMetadataService::rewriteCommentLines(
                {}, {{"proofreading", ReviewRoundService::encode(round, project.filePath())}}));
        }
        LabelPlusDocument::saveToFile(project, project.filePath());
        const auto original = get(project.filePath());
        const auto snapshot = LabelPlusDocument::serialize(project);
        TransferLimits limits;
        limits.projectLabels = 3;
        limits.pageLabels = 2;
        ProjectTransferService service(directory.filePath("app"), limits);
        const auto imported = service.importFile(project.filePath());
        const auto exported = service.exportZip(project);
        QCOMPARE(imported.has_value(), accepted);
        QCOMPARE(exported.has_value(), accepted);
        QCOMPARE(get(project.filePath()), original);
        QCOMPARE(LabelPlusDocument::serialize(project), snapshot);
        if (accepted) {
            const auto loaded = LabelPlusDocument::loadFromFile(imported->projectPath);
            if (target == "live") {
                QCOMPARE(loaded.images().size(), 2);
                QCOMPARE(loaded.images().at(0).labels.size(), 2);
                QCOMPARE(loaded.images().at(1).labels.size(), 1);
            }
            else {
                const auto round = ReviewRoundService::read(loaded);
                QVERIFY(round && round->has_value());
                const auto& countedSnapshot = target == "after" ? *(**round).after : (**round).before;
                QCOMPARE(countedSnapshot.images().size(), 2);
                QCOMPARE(countedSnapshot.images().at(0).labels.size(), 2);
                QCOMPARE(countedSnapshot.images().at(1).labels.size(), 1);
            }
        }
        else {
            QVERIFY(imported.error().contains("size limit"));
            QVERIFY(exported.error().contains("size limit"));
            noPublication(service, directory.filePath("app"));
        }
    }

    void labelCountCapsCannotBeRaisedAndCountTombstones()
    {
        QTemporaryDir directory;
        auto project = withLabelCounts(fixture(directory.filePath("source")), {3});
        project.images()[0].labels[2].setDeleted(true);
        TransferLimits limits;
        QCOMPARE(limits.projectLabels, 100000);
        QCOMPARE(limits.pageLabels, 2000);
        for (const auto& field : {&TransferLimits::projectLabels, &TransferLimits::pageLabels}) {
            auto raised = limits;
            ++(raised.*field);
            const auto result = ProjectTransferService(directory.filePath("app"), raised).exportZip(project);
            QVERIFY(!result);
            QVERIFY(result.error().contains("Invalid transfer limits"));
            raised.*field = 0;
            QVERIFY(!ProjectTransferService(directory.filePath("app"), raised).importFile(project.filePath()));
        }
        limits.pageLabels = 2;
        ProjectTransferService service(directory.filePath("app"), limits);
        const auto result = service.exportZip(project);
        QVERIFY(!result);
        QVERIFY(result.error().contains("size limit"));
        QVERIFY(project.images().first().labels.last().isDeleted());
        noPublication(service, directory.filePath("app"));
    }

    void listingRejectsOverBudgetSavedWorkspace()
    {
        QTemporaryDir directory;
        auto project = withLabelCounts(fixture(directory.filePath("source")), {3});
        LabelPlusDocument::saveToFile(project, project.filePath());
        const auto root = directory.filePath("app");
        const auto imported = ProjectTransferService(root).importFile(project.filePath());
        QVERIFY(imported.has_value());
        TransferLimits limits;
        limits.pageLabels = 2;
        QVERIFY(ProjectTransferService(root, limits).listWorkspaces().isEmpty());
        QVERIFY(QFileInfo::exists(imported->projectPath));
    }

    void exportIsImmutablePrivateAndRoundTrips()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project.setSourceName(directory.filePath("private/source.txt"));
        ReviewRound round;
        round.before = ReviewRoundService::snapshot(project);
        round.before.images()[0].name = "historical-name.png";
        round.after = ReviewRoundService::snapshot(project);
        const QJsonObject metadata{
            {"proofreading", ReviewRoundService::encode(round, project.filePath())},
            {"mergeSources",
             QJsonArray{QJsonObject{
                 {"sourceIndex", 1}, {"sourcePath", "../private/origin.txt"}, {"pageCount", 1}, {"labelCount", 1}}}}};
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines({"User comment stays"}, metadata));
        const auto snapshot = LabelPlusDocument::serialize(project);
        const auto disk = get(project.filePath());
        ProjectTransferService service(directory.filePath("app"));
        const auto exported = service.exportZip(project);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString() : exported.error()));
        const auto firstBytes = get(*exported);
        const auto imported = service.importFile(*exported);
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString() : imported.error()));
        auto loaded = LabelPlusDocument::loadFromFile(imported->projectPath);
        QCOMPARE(loaded.sourceName(), "source.txt");
        QCOMPARE(loaded.id(), project.id());
        QVERIFY(loaded.commentLines().contains("User comment stays"));
        const auto decoded = ProjectMetadataService::metadataObject(loaded.commentLines());
        QVERIFY(!QJsonDocument(decoded).toJson().contains(directory.path().toUtf8()));
        QVERIFY(!QJsonDocument(decoded).toJson().contains("../"));
        const auto restored = ReviewRoundService::read(loaded);
        QVERIFY(restored && restored->has_value());
        QCOMPARE((**restored).before.images().first().name, "historical-name.png");
        QCOMPARE(get((**restored).before.images().first().path), "original image bytes");
        QCOMPARE(LabelPlusDocument::serialize(project), snapshot);
        QCOMPARE(get(project.filePath()), disk);
        project.images()[0].labels[0].setText("later edit");
        const auto next = service.exportZip(project);
        QVERIFY(next.has_value());
        QVERIFY(*next != *exported);
        QCOMPARE(get(*exported), firstBytes);
        QVERIFY(!(QFileInfo(*exported).permissions() & QFileDevice::WriteOwner));
    }

    void safeImagePathsAreCopiedAndRelocated()
    {
        QTemporaryDir directory;
        const auto input = directory.filePath("work.txt");
        put(directory.filePath("images/original.png"), "pixels");
        put(input,
            document({"page.png"},
                     {{"imagePaths",
                       QJsonObject{{"version", 1}, {"paths", QJsonObject{{"page.png", "images/original.png"}}}}}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto imported = service.importFile(input);
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString() : imported.error()));
        const auto project = LabelPlusDocument::loadFromFile(imported->projectPath);
        QCOMPARE(QFileInfo(project.images().first().path).fileName(), "page.png");
        QCOMPARE(get(project.images().first().path), "pixels");
    }

    void utf8NamesAndDeflate()
    {
        QTemporaryDir directory;
        const auto name = QString::fromUtf8(QByteArray::fromHex("e6b58be8af952e706e67"));
        const auto title = QString::fromUtf8(QByteArray::fromHex("e8af91e7a8bf"));
        put(directory.filePath("input.zip"), zip({{(title + ".txt").toUtf8(), document({name})},
                                                  {name.toUtf8(), QByteArray(4096, 'a'), 0100600, 0x0800, 8}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.importFile(directory.filePath("input.zip"));
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QCOMPARE(result->title, QStringLiteral("input"));
        const auto project = LabelPlusDocument::loadFromFile(result->projectPath);
        QCOMPARE(project.images().first().name, name);
        QCOMPARE(get(project.images().first().path), QByteArray(4096, 'a'));
        const auto exported = service.exportZip(project);
        QVERIFY2(exported.has_value(), qPrintable(exported ? QString() : exported.error()));
        QVERIFY(service.importFile(*exported).has_value());
    }

    void bomEncodedImports_data()
    {
        QTest::addColumn<int>("encoding");
        QTest::addColumn<bool>("archive");
        for (const auto encoding : {QStringConverter::Utf8, QStringConverter::Utf16LE, QStringConverter::Utf16BE,
                                    QStringConverter::Utf32LE, QStringConverter::Utf32BE}) {
            for (const bool archive : {false, true}) {
                const auto name = QByteArray(QStringConverter::nameForEncoding(encoding)) + (archive ? "-zip" : "-txt");
                QTest::newRow(name.constData()) << static_cast<int>(encoding) << archive;
            }
        }
    }

    void bomEncodedImports()
    {
        QFETCH(int, encoding);
        QFETCH(bool, archive);
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        const auto text = QString::fromUtf16(u"\u4e2d\u6587\U0001f642\r\n\n");
        project.images()[0].labels[0].setText(text);
        project.images()[0].path = directory.filePath("source/images/actual.png");
        put(project.images().first().path, "mapped image");
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding), QStringConverter::Flag::WriteBom);
        const QByteArray bytes = encoder.encode(LabelPlusDocument::serialize(project));
        put(project.filePath(), bytes);
        const auto shared = LabelPlusDocument::loadFromFile(project.filePath());
        QCOMPARE(shared.images().first().labels.first().text(), text);
        auto input = project.filePath();
        if (archive) {
            input = directory.filePath("input.zip");
            put(input, zip({{"book/work.txt", bytes}, {"book/images/actual.png", "mapped image"}}));
        }
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.importFile(input);
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        const auto loaded = LabelPlusDocument::loadFromFile(result->projectPath);
        QCOMPARE(loaded.id(), shared.id());
        QCOMPARE(loaded.images().first().id, shared.images().first().id);
        QCOMPARE(loaded.images().first().labels.first().id(), shared.images().first().labels.first().id());
        QCOMPARE(loaded.images().first().labels.first().text(), text);
        QCOMPARE(get(loaded.images().first().path), "mapped image");
        QCOMPARE(get(project.filePath()), bytes);
    }

    void invalidBomEncodedInputDoesNotPublish()
    {
        QTemporaryDir directory;
        ProjectTransferService service(directory.filePath("app"));
        const auto input = directory.filePath("input.txt");
        for (const auto encoding : {QStringConverter::Utf16LE, QStringConverter::Utf16BE, QStringConverter::Utf32LE,
                                    QStringConverter::Utf32BE}) {
            QStringEncoder encoder(encoding, QStringConverter::Flag::WriteBom);
            QByteArray bytes = encoder.encode(QString::fromUtf8(document()));
            bytes.chop(1);
            put(input, bytes);
            QVERIFY2(!service.importFile(input), QStringConverter::nameForEncoding(encoding));
            const QJsonObject paths{{"version", 1}, {"paths", QJsonObject{{"unused.png", "../external.png"}}}};
            put(input, encoder.encode(QString::fromUtf8(document({"page.png"}, {{"imagePaths", paths}}))));
            QVERIFY(!service.importFile(input));
        }
        noPublication(service, directory.filePath("app"));
    }

    void ownerMetadataAndEmptyNewRoundSession()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project.images()[0].path = directory.filePath("source/images/actual.png");
        put(project.images().first().path, "mapped image");
        project = withCurrentMetadata(project);
        const auto metadata = ProjectMetadataService::metadataObject(project.commentLines());
        for (const auto& key : {"identities", "labelText", "imagePaths", "review", "reviewSession", "proofreading"})
            QCOMPARE(metadata.value(key).toObject().value("version").toInt(), 1);
        QVERIFY(metadata.value("proofreading").toObject().value("session").toObject().isEmpty());
        QVERIFY(!metadata.value("proofreading").toObject().contains("after"));
        const auto range = metadata.value("mergeSources").toArray().first().toObject();
        QCOMPARE(range.value("firstImage").toString(), "page.png");
        QCOMPARE(range.value("lastImage").toString(), "page.png");
        LabelPlusDocument::saveToFile(project, project.filePath());
        ProjectTransferService service(directory.filePath("app"));
        const auto imported = service.importFile(project.filePath());
        QVERIFY2(imported.has_value(), qPrintable(imported ? QString() : imported.error()));
        const auto output = service.exportZip(LabelPlusDocument::loadFromFile(imported->projectPath));
        QVERIFY2(output.has_value(), qPrintable(output ? QString() : output.error()));
        const auto reimported = service.importFile(*output);
        QVERIFY(reimported.has_value());
        const auto loaded = LabelPlusDocument::loadFromFile(reimported->projectPath);
        const auto restored = ProjectMetadataService::metadataObject(loaded.commentLines());
        QVERIFY(restored.value("proofreading").toObject().value("session").toObject().isEmpty());
        QCOMPARE(restored.value("proofreading").toObject().value("id"),
                 metadata.value("proofreading").toObject().value("id"));
        QCOMPARE(restored.value("review"), metadata.value("review"));
        QCOMPARE(restored.value("reviewSession"), metadata.value("reviewSession"));
        QCOMPARE(restored.value("mergeSources").toArray().first().toObject().value("firstImage"),
                 range.value("firstImage"));
        QCOMPARE(restored.value("mergeSources").toArray().first().toObject().value("sourcePath").toString(),
                 "origin.txt");
        QCOMPARE(loaded.images().first().labels.first().text(), "hello\n");
    }

    void unsupportedMetadataVersion_data()
    {
        QTest::addColumn<QString>("section");
        for (const auto& section : {"identities", "labelText", "imagePaths", "review", "reviewSession", "proofreading"})
            QTest::newRow(section) << QString::fromLatin1(section);
    }

    void unsupportedMetadataVersion()
    {
        QFETCH(QString, section);
        QTemporaryDir directory;
        auto project = withCurrentMetadata(fixture(directory.filePath("source")));
        auto metadata = ProjectMetadataService::metadataObject(project.commentLines());
        auto value = metadata.value(section).toObject();
        value.insert("version", 2);
        metadata.insert(section, value);
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines({}, metadata));
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(!service.exportZip(project));
        noPublication(service, directory.filePath("app"));
    }

    void unknownNestedMetadataIsNotDropped_data()
    {
        QTest::addColumn<QString>("target");
        for (const auto& target : {"identities", "labelText", "review", "session", "annotation", "pair", "round",
                                   "snapshot", "page", "label", "note"})
            QTest::newRow(target) << QString::fromLatin1(target);
    }

    void unknownNestedMetadataIsNotDropped()
    {
        QFETCH(QString, target);
        QTemporaryDir directory;
        auto project = withCurrentMetadata(fixture(directory.filePath("source")));
        auto metadata = ProjectMetadataService::metadataObject(project.commentLines());
        const auto poison = [](QJsonObject object) {
            object.insert("futurePath", "/private/secret");
            return object;
        };
        const auto poisonFirst = [&](QJsonArray array) {
            array[0] = poison(array.first().toObject());
            return array;
        };
        if (target == "identities") {
            auto identities = metadata.value("identities").toObject();
            auto pages = identities.value("pages").toObject();
            pages.insert("page.png", poison(pages.value("page.png").toObject()));
            identities.insert("pages", pages);
            metadata.insert("identities", identities);
        }
        else if (target == "labelText" || target == "review") {
            auto section = metadata.value(target).toObject();
            const auto key = target == "labelText" ? "entries" : "labels";
            section.insert(key, poisonFirst(section.value(key).toArray()));
            metadata.insert(target, section);
        }
        else if (target == "session" || target == "annotation" || target == "pair") {
            auto session = metadata.value("reviewSession").toObject();
            if (target == "session")
                session = poison(session);
            else if (target == "annotation") {
                auto annotations = session.value("annotations").toObject();
                annotations.insert("change", poison(annotations.value("change").toObject()));
                session.insert("annotations", annotations);
            }
            else {
                ReviewSession source;
                ReviewLabelSnapshot label{"page.png", "text", "dialogue", {0.2, 0.3}, 0};
                source.pairs.append({label, label});
                session = ReviewSessionService::encode(source);
                session.insert("pairs", poisonFirst(session.value("pairs").toArray()));
            }
            metadata.insert("reviewSession", session);
        }
        else {
            auto round = metadata.value("proofreading").toObject();
            if (target == "round")
                round = poison(round);
            else if (target == "note") {
                auto decoded = ReviewRoundService::read(project);
                QVERIFY(decoded && decoded->has_value());
                ProofreadNote note;
                note.pageId = project.images().first().id;
                note.pageName = "page.png";
                note.text = "a note";
                (**decoded).notes.append(note);
                round = ReviewRoundService::encode(**decoded, project.filePath());
                round.insert("notes", poisonFirst(round.value("notes").toArray()));
            }
            else {
                auto snapshot = round.value("before").toObject();
                if (target == "snapshot")
                    snapshot = poison(snapshot);
                else {
                    auto pages = snapshot.value("pages").toArray();
                    auto page = pages.first().toObject();
                    if (target == "page")
                        page = poison(page);
                    else
                        page.insert("labels", poisonFirst(page.value("labels").toArray()));
                    pages[0] = page;
                    snapshot.insert("pages", pages);
                }
                round.insert("before", snapshot);
            }
            metadata.insert("proofreading", round);
        }
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines({}, metadata));
        ProjectTransferService service(directory.filePath("app"));
        const auto original = project.commentLines();
        QVERIFY(!service.exportZip(project));
        QCOMPARE(project.commentLines(), original);
        noPublication(service, directory.filePath("app"));
    }

    void identityValidationRetainsExternalEditAndDeletedLabelSemantics()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project = LabelPlusDocument::loadFromFile(project.filePath());
        // Metadata refers to the saved body; exporting a later unsaved edit must not restore stale text/IDs.
        project.images()[0].labels[0].setText("unsaved change");
        const auto id = project.images().first().labels.first().id();
        ProjectTransferService service(directory.filePath("app"));
        const auto output = service.exportZip(project);
        QVERIFY2(output.has_value(), qPrintable(output ? QString() : output.error()));
        const auto imported = service.importFile(*output);
        QVERIFY(imported.has_value());
        const auto loaded = LabelPlusDocument::loadFromFile(imported->projectPath);
        QCOMPARE(loaded.images().first().labels.first().id(), id);
        QCOMPARE(loaded.images().first().labels.first().text(), "unsaved change");
        project.images()[0].labels[0].setDeleted(true);
        // Persisted identities omit tombstones, while the live Project retains them for undo.
        const auto serialized = LabelPlusDocument::parse(LabelPlusDocument::serialize(project), project.filePath());
        project.setCommentLines(serialized.commentLines());
        QVERIFY(service.exportZip(project).has_value());
    }

    void compressedBombWithFalseDeclaredSize()
    {
        QTemporaryDir directory;
        auto bytes = zip({{"page.png", QByteArray(1024 * 1024, 'x'), 0100600, 0x0800, 8}, {"work.txt", document()}});
        const auto central = bytes.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central > 0);
        qToLittleEndian<quint32>(1, bytes.data() + 22);
        qToLittleEndian<quint32>(1, bytes.data() + central + 24);
        put(directory.filePath("bomb.zip"), bytes);
        TransferLimits limits;
        limits.fileBytes = 2048;
        limits.totalBytes = 4096;
        ProjectTransferService service(directory.filePath("app"), limits);
        QVERIFY(!service.importFile(directory.filePath("bomb.zip")));
        noPublication(service, directory.filePath("app"));
    }

    void historicalImagesAndMissingHistory()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        ReviewRound round;
        round.before = ReviewRoundService::snapshot(project);
        round.before.images()[0].path = directory.filePath("source/old.png");
        put(round.before.images()[0].path, "historical pixels");
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines(
            {}, {{"proofreading", ReviewRoundService::encode(round, project.filePath())}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto output = service.exportZip(project);
        QVERIFY2(output.has_value(), qPrintable(output ? QString() : output.error()));
        const auto imported = service.importFile(*output);
        QVERIFY(imported.has_value());
        const auto restored = ReviewRoundService::read(LabelPlusDocument::loadFromFile(imported->projectPath));
        QVERIFY(restored && restored->has_value());
        QCOMPARE(get((**restored).before.images().first().path), "historical pixels");
        QVERIFY(QFile::remove(round.before.images().first().path));
        const auto missing = service.exportZip(project);
        QVERIFY(!missing);
        QVERIFY(missing.error().contains("missing"));
        LabelPlusDocument::saveToFile(project, project.filePath());
        const auto missingImport = service.importFile(project.filePath());
        QVERIFY(missingImport.has_value());
        QVERIFY(!service.exportZip(LabelPlusDocument::loadFromFile(missingImport->projectPath)));
        round.before.images()[0].path.clear();
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines(
            {}, {{"proofreading", ReviewRoundService::encode(round, project.filePath())}}));
        QVERIFY(service.exportZip(project).has_value());
    }

    void externalAndUnknownHistoricalPathsAreRejected()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        ReviewRound round;
        round.before = ReviewRoundService::snapshot(project);
        auto section = ReviewRoundService::encode(round, project.filePath());
        section.insert("futurePaths", QJsonObject{{"local", "/private/image.png"}});
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines({}, {{"proofreading", section}}));
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(!service.exportZip(project));
        section = ReviewRoundService::encode(round, project.filePath());
        auto before = section.value("before").toObject();
        auto pages = before.value("pages").toArray();
        auto page = pages.first().toObject();
        page.insert("path", "../external.png");
        pages[0] = page;
        before.insert("pages", pages);
        section.insert("before", before);
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines({}, {{"proofreading", section}}));
        QVERIFY(!service.exportZip(project));
        LabelPlusDocument::saveToFile(project, project.filePath());
        QVERIFY(!service.importFile(project.filePath()));
        project.setCommentLines({});
        put(directory.filePath("external.png"), "private pixels");
        project.images()[0].path = directory.filePath("external.png");
        QVERIFY(!service.exportZip(project));
        noPublication(service, directory.filePath("app"));
    }

    void historicalTxtCannotCreateAmbiguousZip()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        ReviewRound round;
        round.before = ReviewRoundService::snapshot(project);
        round.before.images()[0].path = directory.filePath("source/history.txt");
        put(round.before.images()[0].path, "not an image");
        project.setCommentLines(ProjectMetadataService::rewriteCommentLines(
            {}, {{"proofreading", ReviewRoundService::encode(round, project.filePath())}}));
        ProjectTransferService service(directory.filePath("app"));
        const auto result = service.exportZip(project);
        QVERIFY(!result);
        QVERIFY(result.error().contains("TXT extension"));
        noPublication(service, directory.filePath("app"));
    }

    void exportCollisionAndBudgetFailureDoNotPublish()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project.images().append({"PAGE.png", project.images().first().path, {}});
        ProjectTransferService service(directory.filePath("app"));
        QVERIFY(!service.exportZip(project));
        project.images().removeLast();
        TransferLimits limits;
        limits.inputBytes = 32;
        QVERIFY(!ProjectTransferService(directory.filePath("app"), limits).exportZip(project));
        noPublication(service, directory.filePath("app"));
    }

    void malformedTxtDoesNotPublish()
    {
        QTemporaryDir directory;
        ProjectTransferService service(directory.filePath("app"));
        const QJsonObject unsupportedPaths{{"version", 2}, {"paths", QJsonObject{}}};
        for (const auto& bytes :
             {QByteArray("not a project"), document() + QByteArray(1, '\0'), document() + QByteArray::fromHex("ff"),
              document() + QByteArray::fromHex("c3"), document({}, {{"imagePaths", unsupportedPaths}})}) {
            put(directory.filePath("bad.txt"), bytes);
            QVERIFY(!service.importFile(directory.filePath("bad.txt")));
        }
        noPublication(service, directory.filePath("app"));
    }

    void unknownMetadataFailsWithoutLoss()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        project.setCommentLines(
            ProjectMetadataService::rewriteCommentLines({}, {{"future", QJsonObject{{"path", "/private"}}}}));
        LabelPlusDocument::saveToFile(project, project.filePath());
        ProjectTransferService service(directory.filePath("app"));
        const auto original = get(project.filePath());
        QVERIFY(!service.importFile(project.filePath()));
        QVERIFY(!service.exportZip(project));
        QCOMPARE(get(project.filePath()), original);
        const QJsonObject unexpectedNested{{"path", "/private"}};
        const QJsonObject malformedSource{
            {"sourceIndex", unexpectedNested}, {"sourcePath", "origin.txt"}, {"pageCount", 1}, {"labelCount", 1}};
        project.setCommentLines(
            ProjectMetadataService::rewriteCommentLines({}, {{"mergeSources", QJsonArray{malformedSource}}}));
        QVERIFY(!service.exportZip(project));
        noPublication(service, directory.filePath("app"));
    }

    void listingIgnoresCorruptAndUnpublishedWorkspaces()
    {
        QTemporaryDir directory;
        auto project = fixture(directory.filePath("source"));
        const auto root = directory.filePath("app");
        ProjectTransferService service(root);
        const auto imported = service.importFile(project.filePath());
        QVERIFY(imported.has_value());
        put(QDir(root).filePath(".staging-import-abandoned/candidate/project/project.txt"), document());
        put(QDir(root).filePath("workspaces/not-a-workspace/project/project.txt"), document());
        QCOMPARE(service.listWorkspaces().size(), 1);
        put(imported->projectPath, "corrupt");
        QCOMPARE(service.listWorkspaces().size(), 0);
        QVERIFY(QFileInfo::exists(imported->projectPath));
        QCOMPARE(get(project.filePath()), LabelPlusDocument::serialize(project).toUtf8());
    }

    void failedPublicationPreservesExistingWorkspace()
    {
        QTemporaryDir directory;
        const auto project = fixture(directory.filePath("source"));
        const auto root = directory.filePath("app");
        ProjectTransferService service(root);
        const auto imported = service.importFile(project.filePath());
        QVERIFY(imported.has_value());
        const auto original = get(imported->projectPath);
        put(directory.filePath("bad.zip"), zip({{"work.txt", document()}, {"../escape", "bad"}}));
        QVERIFY(!service.importFile(directory.filePath("bad.zip")));
        put(QDir(root).filePath("outgoing"), "blocked destination");
        QVERIFY(!service.exportZip(project));
        QCOMPARE(service.listWorkspaces().size(), 1);
        QCOMPARE(get(imported->projectPath), original);
    }
};

QTEST_GUILESS_MAIN(ProjectTransferTests)
#include "ProjectTransferTests.moc"
