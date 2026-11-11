#include "services/transfer/TransferSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
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

// libarchive may finish a volume at a file boundary without asking for another one.
// Reject the archive-level volume flag up front instead of publishing a partial project.
void checkRarEnvelope(QFile& file, ArchiveFormat format, const QString& error)
{
    const auto header = file.peek(128);
    if (format == ArchiveFormat::Rar) {
        require(header.size() >= 20 && static_cast<quint8>(header[9]) == 0x73 && (u16(header, 10) & 0x0081) == 0,
                error);
        return;
    }
    qsizetype offset = 12; // RAR5 signature + first header CRC32.
    const auto readVar = [&]() -> quint64 {
        quint64 value = 0;
        for (int shift = 0; shift < 63; shift += 7) {
            require(offset < header.size(), error);
            const auto byte = static_cast<quint8>(header[offset++]);
            value |= quint64(byte & 0x7f) << shift;
            if (!(byte & 0x80))
                return value;
        }
        fail(error);
    };
    const auto size = readVar();
    const auto start = offset;
    require(size <= static_cast<quint64>(file.size() - offset) && readVar() == 1, error);
    const auto flags = readVar();
    require((flags & ~quint64(5)) == 0, error); // Optional extra area and skip-if-unknown flag only.
    if (flags & 1)
        (void)readVar();
    require((readVar() & 3) == 0 && static_cast<quint64>(offset - start) <= size, error);
}

void checkGzipEnvelope(QFile& file, const TransferLimits& limits, const QString& error)
{
    // libarchive's gzip filter does not verify CRC/ISIZE, and tar may stop before the gzip trailer.
    // Validate the whole stream using zlib's gzip wrapper without buffering the expanded archive.
    z_stream stream{};
    require(inflateInit2(&stream, MAX_WBITS + 16) == Z_OK, error);
    const auto cleanup = qScopeGuard([&stream] { inflateEnd(&stream); });
    std::array<Bytef, chunkSize> input, output;
    // Bound container headers/padding as well as file contents (including ignored tar metadata).
    qint64 remaining = limits.totalBytes + qint64(limits.entries) * chunkSize + 1024 * 1024;
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        if (stream.avail_in == 0) {
            const auto read = file.read(reinterpret_cast<char*>(input.data()), input.size());
            require(read > 0, error);
            stream.next_in = input.data();
            stream.avail_in = static_cast<uInt>(read);
        }
        stream.next_out = output.data();
        stream.avail_out = static_cast<uInt>(output.size());
        status = inflate(&stream, Z_NO_FLUSH);
        require(status == Z_OK || status == Z_STREAM_END, error);
        const auto produced = static_cast<qint64>(output.size() - stream.avail_out);
        require(produced <= remaining, ProjectTransferService::tr("Transfer size limit exceeded."));
        remaining -= produced;
    }
    // Permit conventional zero padding, not concatenated members or hidden trailing payloads.
    require(std::all_of(stream.next_in, stream.next_in + stream.avail_in, [](Bytef ch) { return ch == 0; }), error);
    while (!file.atEnd()) {
        const auto bytes = file.read(chunkSize);
        require(!bytes.isEmpty() && std::all_of(bytes.cbegin(), bytes.cend(), [](char ch) { return ch == 0; }), error);
    }
    require(file.error() == QFileDevice::NoError, error);
}

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

QStringList extractZip(const QString& input, const QString& destination, const TransferLimits& limits)
{
    return extractArchive(input, destination, ArchiveFormat::Zip, limits);
}

std::optional<ArchiveFormat> detectArchive(QFile& input)
{
    const auto magic = input.peek(512);
    if (magic.startsWith("PK"))
        return ArchiveFormat::Zip;
    if (magic.startsWith(QByteArray::fromHex("377abcaf271c")))
        return ArchiveFormat::SevenZip;
    if (magic.startsWith(QByteArray::fromHex("526172211a070100")))
        return ArchiveFormat::Rar5;
    if (magic.startsWith(QByteArray::fromHex("526172211a0700")))
        return ArchiveFormat::Rar;
    if (magic.startsWith(QByteArray::fromHex("1f8b")))
        return ArchiveFormat::TarGzip;
    if (magic.mid(257, 5) == "ustar")
        return ArchiveFormat::Tar;
    return std::nullopt;
}

QStringList extractArchive(const QString& input, const QString& destination, ArchiveFormat format,
                           const TransferLimits& limits)
{
    const bool zip = format == ArchiveFormat::Zip;
    const bool tar = format == ArchiveFormat::Tar || format == ArchiveFormat::TarGzip;
    const auto error =
        zip ? zipError()
            : ProjectTransferService::tr(
                  "Invalid or unsupported archive. Use an unencrypted, single-volume ZIP, 7z, RAR, tar or tar.gz.");
    QFile file(input);
    require(file.open(QIODevice::ReadOnly), ioError(input));
    if (format == ArchiveFormat::Rar || format == ArchiveFormat::Rar5)
        checkRarEnvelope(file, format, error);
    if (format == ArchiveFormat::TarGzip)
        checkGzipEnvelope(file, limits, error);
    QHash<QString, ZipEntry> expected;
    if (zip)
        for (const auto& entry : zipEnvelope(file, limits))
            expected.insert(entry.path, entry);
    require(file.seek(0), error);
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    require(bool(reader), error);
    int supported = ARCHIVE_FATAL;
    switch (format) {
    case ArchiveFormat::Zip:
        supported = archive_read_support_format_zip_seekable(reader.get());
        require(archive_read_set_format_option(reader.get(), "zip", "hdrcharset", "UTF-8") == ARCHIVE_OK, error);
        break;
    case ArchiveFormat::SevenZip:
        supported = archive_read_support_format_7zip(reader.get());
        break;
    case ArchiveFormat::Rar:
        supported = archive_read_support_format_rar(reader.get());
        break;
    case ArchiveFormat::Rar5:
        supported = archive_read_support_format_rar5(reader.get());
        break;
    case ArchiveFormat::TarGzip:
        require(archive_read_support_filter_gzip(reader.get()) == ARCHIVE_OK, error);
        [[fallthrough]];
    case ArchiveFormat::Tar:
        supported = archive_read_support_format_tar(reader.get());
        break;
    }
    require(supported == ARCHIVE_OK && archive_read_open_fd(reader.get(), file.handle(), chunkSize) == ARCHIVE_OK,
            error);
    QStringList files;
    qint64 remaining = limits.totalBytes;
    int entries = 0;
    Names names;
    archive_entry* entry = nullptr;
    int status;
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        require(++entries <= limits.entries, ProjectTransferService::tr("Transfer entry limit exceeded."));
        const auto rawName = archive_entry_pathname_utf8(entry);
        require(rawName && QByteArray(rawName).isValidUtf8(), error);
        QString path = QString::fromUtf8(rawName);
        // POSIX tar writers conventionally prefix members with ./ and include the root directory.
        if (tar)
            while (path.startsWith("./"))
                path.remove(0, 2);
        if (path.endsWith('/'))
            path.chop(1);
        const bool directory = archive_entry_filetype(entry) == AE_IFDIR;
        require(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry) &&
                    archive_entry_is_encrypted(entry) == 0 &&
                    (directory || archive_entry_filetype(entry) == AE_IFREG) &&
                    archive_entry_sparse_count(entry) == 0 && archive_entry_size_is_set(entry),
                error);
        const auto size = archive_entry_size(entry);
        require(size >= 0 && size <= limits.fileBytes && size <= remaining && (!directory || size == 0),
                ProjectTransferService::tr("Transfer size limit exceeded."));
        if (tar && directory && (path.isEmpty() || path == "."))
            continue;
        names.add(relativePath(path, limits), directory);
        ZipEntry info{path, size, 0, directory};
        if (zip) {
            require(expected.contains(path), error);
            info = expected.take(path);
            require(info.directory == directory && info.size == size, error);
        }
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
            require(read == 0 && count == size && (!zip || crc == info.crc) && output.flush(), error);
            // Nested archives are never recursively processed; reject known archive containers explicitly.
            const auto suffix = QFileInfo(path).suffix().toLower();
            require(suffix != "zip" && suffix != "7z" && suffix != "rar" && suffix != "tar" && suffix != "gz" &&
                        suffix != "tgz",
                    ProjectTransferService::tr("Nested archives are not supported."));
            output.close();
            QFile probe(target);
            require(probe.open(QIODevice::ReadOnly), ioError(target));
            require(!detectArchive(probe), ProjectTransferService::tr("Nested archives are not supported."));
            files.append(path);
        }
    }
    require(status == ARCHIVE_EOF && expected.isEmpty() && archive_read_has_encrypted_entries(reader.get()) <= 0 &&
                archive_read_close(reader.get()) == ARCHIVE_OK,
            error);
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
