#include "services/transfer/TransferSupport.h"

#include "core/LabelPlusDocument.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QtEndian>
#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <memory>

namespace labelqt::services::transfer {
namespace {
constexpr qint64 chunkSize = 65536;

QString ioError(const QString& path)
{
    return ProjectTransferService::tr("Transfer could not read or write a regular local file: %1").arg(path);
}

QByteArray digestFile(const QString& path, qint64 limit)
{
    noLinks(path);
    QFile file(path);
    require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly), ioError(path));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!file.atEnd()) {
        const auto bytes = file.read(chunkSize);
        require(file.error() == QFileDevice::NoError && !bytes.isEmpty(), ioError(path));
        require(bytes.size() <= limit - count, ProjectTransferService::tr("Transfer size limit exceeded."));
        count += bytes.size();
        hash.addData(bytes);
    }
    return hash.result();
}

struct ZipEntry {
    QString path;
    qint64 size;
    quint32 crc;
    bool directory;
};

QString zipError()
{
    return ProjectTransferService::tr(
        "Invalid or unsupported ZIP. Use an unencrypted, single-volume ZIP with UTF-8 names, store or "
        "deflate compression, and no ZIP64 extensions.");
}

QByteArray bytesAt(QFile& file, qint64 offset, qint64 size)
{
    require(offset >= 0 && size >= 0 && size <= file.size() - offset && file.seek(offset), zipError());
    const auto bytes = file.read(size);
    require(bytes.size() == size, zipError());
    return bytes;
}

quint16 u16(const QByteArray& bytes, int offset)
{
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}
quint32 u32(const QByteArray& bytes, int offset)
{
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}

void checkExtra(const QByteArray& bytes)
{
    qsizetype pos = 0;
    while (pos < bytes.size()) {
        require(bytes.size() - pos >= 4, zipError());
        const auto tag = u16(bytes, static_cast<int>(pos));
        const auto size = u16(bytes, static_cast<int>(pos + 2));
        // Reject alternate pathname/ZIP64/encryption encodings rather than let two name parsers disagree.
        require(tag != 0x0001 && tag != 0x7075 && tag != 0x9901 && tag != 0x0017, zipError());
        pos += 4;
        require(size <= bytes.size() - pos, zipError());
        pos += size;
    }
}

QVector<ZipEntry> zipEnvelope(QFile& file, const TransferLimits& limits)
{
    require(file.size() >= 22 && file.size() <= limits.inputBytes, zipError());
    const auto tail = bytesAt(file, std::max<qint64>(0, file.size() - 65557), std::min<qint64>(file.size(), 65557));
    qsizetype end = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail, static_cast<int>(i)) == 0x06054b50 &&
            i + 22 + u16(tail, static_cast<int>(i + 20)) == tail.size()) {
            end = i;
            break;
        }
    }
    require(end >= 0, zipError());
    const auto eocd = tail.mid(end, 22);
    const auto count = u16(eocd, 10);
    const qint64 centralSize = u32(eocd, 12), centralOffset = u32(eocd, 16);
    require(u16(eocd, 4) == 0 && u16(eocd, 6) == 0 && u16(eocd, 8) == count && count != 0xffff &&
                centralOffset != 0xffffffff && centralSize != 0xffffffff &&
                centralOffset + centralSize == file.size() - tail.size() + end,
            zipError());
    require(count <= limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
    QVector<ZipEntry> entries;
    Names names;
    qint64 offset = centralOffset, remaining = limits.totalBytes;
    QVector<QPair<qint64, qint64>> ranges;
    for (int index = 0; index < count; ++index) {
        const auto header = bytesAt(file, offset, 46);
        require(u32(header, 0) == 0x02014b50, zipError());
        const auto flags = u16(header, 8), method = u16(header, 10);
        const qint64 compressed = u32(header, 20), size = u32(header, 24), localOffset = u32(header, 42);
        const int nameSize = u16(header, 28), extraSize = u16(header, 30), commentSize = u16(header, 32);
        require((flags & ~0x080e) == 0 && (method == 0 || method == 8) && u16(header, 34) == 0 &&
                    compressed != 0xffffffff && size != 0xffffffff && localOffset < centralOffset,
                zipError());
        require(size <= limits.fileBytes && size <= remaining,
                ProjectTransferService::tr("Transfer size limit exceeded."));
        remaining -= size;
        const auto rawName = bytesAt(file, offset + 46, nameSize);
        require(rawName.isValidUtf8() && !rawName.contains('\0'), zipError());
        QString path = QString::fromUtf8(rawName);
        const bool directory = path.endsWith('/');
        if (directory)
            path.chop(1);
        path = relativePath(path, limits);
        require(!directory || size == 0, zipError());
        names.add(path, directory);
        checkExtra(bytesAt(file, offset + 46 + nameSize, extraSize));
        offset += 46 + nameSize + extraSize + commentSize;
        require(offset <= centralOffset + centralSize, zipError());

        const auto local = bytesAt(file, localOffset, 30);
        require(u32(local, 0) == 0x04034b50 && u16(local, 6) == flags && u16(local, 8) == method &&
                    bytesAt(file, localOffset + 30, u16(local, 26)) == rawName,
                zipError());
        checkExtra(bytesAt(file, localOffset + 30 + u16(local, 26), u16(local, 28)));
        const qint64 dataOffset = localOffset + 30 + u16(local, 26) + u16(local, 28);
        require(compressed <= centralOffset - dataOffset, zipError());
        qint64 rangeEnd = dataOffset + compressed;
        if ((flags & 8) == 0) {
            require(u32(local, 14) == u32(header, 16) && u32(local, 18) == compressed && u32(local, 22) == size,
                    zipError());
        }
        else {
            auto descriptor = bytesAt(file, rangeEnd, 12);
            if (u32(descriptor, 0) == 0x08074b50) {
                rangeEnd += 4;
                descriptor = bytesAt(file, rangeEnd, 12);
            }
            require(u32(descriptor, 0) == u32(header, 16) && u32(descriptor, 4) == compressed &&
                        u32(descriptor, 8) == size,
                    zipError());
            rangeEnd += 12;
        }
        require(rangeEnd <= centralOffset, zipError());
        ranges.append({localOffset, rangeEnd});
        entries.append({path, size, u32(header, 16), directory});
    }
    require(offset == centralOffset + centralSize, zipError());
    std::sort(ranges.begin(), ranges.end());
    qint64 previousEnd = 0;
    for (const auto& range : ranges) {
        require(range.first == previousEnd, zipError());
        previousEnd = range.second;
    }
    require(previousEnd == centralOffset, zipError());
    return entries;
}

struct ZipOutput {
    QFile file;
    qint64 remaining;
};

la_ssize_t writeArchive(archive*, void* client, const void* buffer, size_t size)
{
    auto& output = *static_cast<ZipOutput*>(client);
    if (size > static_cast<quint64>(output.remaining))
        return -1;
    const auto written = output.file.write(static_cast<const char*>(buffer), static_cast<qint64>(size));
    if (written != static_cast<qint64>(size))
        return -1;
    output.remaining -= written;
    return written;
}
} // namespace

void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}
void require(bool condition, const QString& message)
{
    if (!condition)
        fail(message);
}

void checkLimits(const TransferLimits& limits)
{
    require(limits.inputBytes > 0 && limits.fileBytes > 0 && limits.totalBytes > 0 && limits.textBytes > 0 &&
                limits.entries > 0 && limits.depth > 0 && limits.depth <= 32 && limits.entries <= 10000 &&
                limits.inputBytes <= 2LL * 1024 * 1024 * 1024 && limits.fileBytes <= 512LL * 1024 * 1024 &&
                limits.totalBytes <= 8LL * 1024 * 1024 * 1024 && limits.textBytes <= 32LL * 1024 * 1024 &&
                limits.projectLabels > 0 && limits.projectLabels <= TransferLimits{}.projectLabels &&
                limits.pageLabels > 0 && limits.pageLabels <= TransferLimits{}.pageLabels,
            ProjectTransferService::tr("Invalid transfer limits."));
}

QString relativePath(const QString& path, const TransferLimits& limits)
{
    const auto error = ProjectTransferService::tr("Unsafe or nonportable transfer path: %1").arg(path);
    require(!path.isEmpty() && path.toUtf8().size() <= 4096 && path == path.trimmed() &&
                QString::fromUtf8(path.toUtf8()) == path,
            error);
    const auto parts = path.split('/');
    require(parts.size() <= limits.depth, error);
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const auto& part : parts) {
        require(!part.isEmpty() && part != "." && part != ".." && part.toUtf8().size() <= 255 && !part.endsWith('.') &&
                    !part.endsWith(' ') && !device.match(part).hasMatch(),
                error);
        for (const auto ch : part)
            require(ch.unicode() >= 32 && ch.unicode() != 127 && !QStringLiteral("\\:<>\"|?*").contains(ch) &&
                        ch.category() != QChar::Other_Format,
                    error);
    }
    return path;
}

void noLinks(const QString& path)
{
    require(QDir::isAbsolutePath(path) && !path.contains(QChar::Null) && !path.split('/').contains(".."),
            ioError(path));
    QString current = QDir::cleanPath(path);
    for (;;) {
        const QFileInfo info(current);
        require(!info.isSymLink(), ProjectTransferService::tr("Links are not allowed in transfer paths: %1").arg(path));
        const auto parent = info.absolutePath();
        if (parent == current)
            break;
        current = parent;
    }
}

void makeDirectory(const QString& path)
{
    noLinks(path);
    require(QDir().mkpath(path), ioError(path));
    noLinks(path);
}

QByteArray readBounded(const QString& path, qint64 limit)
{
    noLinks(path);
    QFile file(path);
    require(QFileInfo(path).isFile() && file.size() <= limit && file.open(QIODevice::ReadOnly), ioError(path));
    const auto bytes = file.read(limit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= limit, ioError(path));
    return bytes;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    makeDirectory(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush(),
            ioError(path));
}

QByteArray copyFile(const QString& from, const QString& to, qint64 limit, qint64& remaining)
{
    noLinks(from);
    const QFileInfo before(from);
    require(before.isFile() && before.size() <= limit && before.size() <= remaining, ioError(from));
    QFile input(from);
    require(input.open(QIODevice::ReadOnly), ioError(from));
    makeDirectory(QFileInfo(to).absolutePath());
    QFile output(to);
    require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), ioError(to));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!input.atEnd()) {
        const auto bytes = input.read(chunkSize);
        require(input.error() == QFileDevice::NoError && !bytes.isEmpty(), ioError(from));
        require(bytes.size() <= limit - count && bytes.size() <= remaining,
                ProjectTransferService::tr("Transfer size limit exceeded."));
        count += bytes.size();
        remaining -= bytes.size();
        require(output.write(bytes) == bytes.size(), ioError(to));
        hash.addData(bytes);
    }
    require(output.flush(), ioError(to));
    const QFileInfo after(from);
    const auto digest = hash.result();
    require(count == before.size() && after.size() == before.size() && after.lastModified() == before.lastModified() &&
                digestFile(from, limit) == digest,
            ProjectTransferService::tr("A transfer source changed while being copied. Try again."));
    return digest;
}

core::Project readProject(const QString& path, const TransferLimits& limits)
{
    const auto bytes = readBounded(path, limits.textBytes);
    // Match LabelPlusDocument's Qt BOM detection, but reject incomplete/invalid input rather than
    // accepting replacement characters. Keep the bounded byte snapshot; never reopen the source.
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
    const QString text = decoder.decode(bytes);
    // Some Qt versions do not flag a truncated UTF-32 code unit even with Stateless decoding.
    const int unitBytes = encoding == QStringConverter::Utf32LE || encoding == QStringConverter::Utf32BE   ? 4
                          : encoding == QStringConverter::Utf16LE || encoding == QStringConverter::Utf16BE ? 2
                                                                                                           : 1;
    require(bytes.size() % unitBytes == 0 && !decoder.hasError() && !text.contains(QChar::Null),
            QCoreApplication::translate("LabelPlusDocument", "Invalid LabelPlus document structure."));
    return core::LabelPlusDocument::parse(text, path);
}

void Names::add(const QString& path, bool directory)
{
    const auto parts = path.split('/');
    QString spelling;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (i)
            spelling += '/';
        spelling += parts[i];
        const bool last = i + 1 == parts.size(), isDir = !last || directory;
        const auto key = spelling.normalized(QString::NormalizationForm_C).toCaseFolded();
        auto it = m_names.find(key);
        if (it != m_names.end()) {
            require(it->spelling == spelling && it->directory && isDir && !(last && it->explicitEntry),
                    ProjectTransferService::tr("Duplicate or colliding transfer path: %1").arg(path));
            if (last)
                it->explicitEntry = true;
        }
        else {
            m_names.insert(key, {spelling, isDir, last});
        }
    }
}

QStringList extractZip(const QString& input, const QString& destination, const TransferLimits& limits)
{
    QFile file(input);
    require(file.open(QIODevice::ReadOnly), ioError(input));
    const auto envelope = zipEnvelope(file, limits);
    QHash<QString, ZipEntry> expected;
    for (const auto& entry : envelope)
        expected.insert(entry.path, entry);
    require(file.seek(0), zipError());
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    require(reader && archive_read_support_format_zip_seekable(reader.get()) == ARCHIVE_OK &&
                archive_read_set_format_option(reader.get(), "zip", "hdrcharset", "UTF-8") == ARCHIVE_OK &&
                archive_read_open_fd(reader.get(), file.handle(), chunkSize) == ARCHIVE_OK,
            zipError());
    QStringList files;
    qint64 remaining = limits.totalBytes;
    archive_entry* entry = nullptr;
    int status;
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const auto rawName = archive_entry_pathname_utf8(entry);
        require(rawName, zipError());
        QString path = QString::fromUtf8(rawName);
        if (path.endsWith('/'))
            path.chop(1);
        require(expected.contains(path), zipError());
        const auto info = expected.take(path);
        require(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry) &&
                    archive_entry_is_encrypted(entry) == 0 &&
                    archive_entry_filetype(entry) == (info.directory ? AE_IFDIR : AE_IFREG) &&
                    archive_entry_size(entry) == info.size,
                zipError());
        const auto target = QDir(destination).filePath(relativePath(path, limits));
        if (info.directory) {
            makeDirectory(target);
        }
        else {
            makeDirectory(QFileInfo(target).absolutePath());
            QFile output(target);
            require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), ioError(target));
            std::array<char, chunkSize> buffer;
            qint64 count = 0;
            uLong crc = crc32(0, nullptr, 0);
            la_ssize_t read;
            while ((read = archive_read_data(reader.get(), buffer.data(), buffer.size())) > 0) {
                require(read <= limits.fileBytes - count && read <= remaining && read <= info.size - count,
                        ProjectTransferService::tr("Transfer size limit exceeded."));
                count += read;
                remaining -= read;
                crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.data()), static_cast<uInt>(read));
                require(output.write(buffer.data(), read) == read, ioError(target));
            }
            require(read == 0 && count == info.size && crc == info.crc && output.flush(), zipError());
            // Nested archives are never recursively processed; reject known archive containers explicitly.
            const auto suffix = QFileInfo(path).suffix().toLower();
            require(suffix != "zip" && suffix != "7z" && suffix != "rar",
                    ProjectTransferService::tr("Nested archives are not supported."));
            output.close();
            QFile probe(target);
            require(probe.open(QIODevice::ReadOnly), ioError(target));
            const auto magic = probe.peek(8);
            require(!magic.startsWith("PK\003\004") && !magic.startsWith("PK\005\006") && !magic.startsWith("Rar!") &&
                        !magic.startsWith(QByteArray::fromHex("377abcaf271c")),
                    ProjectTransferService::tr("Nested archives are not supported."));
            files.append(path);
        }
    }
    require(status == ARCHIVE_EOF && expected.isEmpty() && archive_read_close(reader.get()) == ARCHIVE_OK, zipError());
    return files;
}

void writeZip(const QString& directory, const QStringList& files, const QString& output, const TransferLimits& limits)
{
    require(files.size() <= limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
    ZipOutput sink{QFile(output), limits.inputBytes};
    require(sink.file.open(QIODevice::WriteOnly | QIODevice::NewOnly), ioError(output));
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
    require(writer && archive_write_set_format_zip(writer.get()) == ARCHIVE_OK &&
                archive_write_set_options(writer.get(), "zip:compression=deflate,zip:!zip64") == ARCHIVE_OK &&
                archive_write_set_bytes_per_block(writer.get(), 0) == ARCHIVE_OK &&
                archive_write_open(writer.get(), &sink, nullptr, writeArchive, nullptr) == ARCHIVE_OK,
            zipError());
    for (const auto& name : files) {
        relativePath(name, limits);
        QFile file(QDir(directory).filePath(name));
        require(file.open(QIODevice::ReadOnly), ioError(file.fileName()));
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        require(entry != nullptr, zipError());
        const auto path = name.toUtf8();
        archive_entry_set_pathname_utf8(entry.get(), path.constData());
        archive_entry_set_size(entry.get(), file.size());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0600);
        require(archive_write_header(writer.get(), entry.get()) == ARCHIVE_OK, zipError());
        while (!file.atEnd()) {
            const auto bytes = file.read(chunkSize);
            require(file.error() == QFileDevice::NoError && !bytes.isEmpty() &&
                        archive_write_data(writer.get(), bytes.constData(), static_cast<size_t>(bytes.size())) ==
                            bytes.size(),
                    ioError(output));
        }
        require(archive_write_finish_entry(writer.get()) == ARCHIVE_OK, zipError());
    }
    require(archive_write_close(writer.get()) == ARCHIVE_OK && sink.file.flush(), ioError(output));
}

} // namespace labelqt::services::transfer
