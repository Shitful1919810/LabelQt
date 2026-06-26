#include "services/ArchiveReader.h"

#ifdef LABELQT_HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include <QFileInfo>

#include <expected>

namespace labelqt::services {

#ifdef LABELQT_HAS_LIBARCHIVE
namespace {
bool isImageName(const QString& name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    return suffix == "jpg" || suffix == "jpeg" || suffix == "png" || suffix == "bmp" || suffix == "webp";
}
} // namespace
#endif

bool ArchiveReader::isAvailable() noexcept
{
#ifdef LABELQT_HAS_LIBARCHIVE
    return true;
#else
    return false;
#endif
}

QStringList ArchiveReader::listImages(const QString& archivePath) const
{
    return tryListImages(archivePath).value_or(QStringList{});
}

std::expected<QStringList, QString> ArchiveReader::tryListImages(const QString& archivePath) const
{
    QStringList images;

#ifdef LABELQT_HAS_LIBARCHIVE
    archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(reader, archivePath.toLocal8Bit().constData(), 10240) == ARCHIVE_OK) {
        archive_entry* entry = nullptr;
        while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
            const char* path = archive_entry_pathname(entry);
            if (path != nullptr) {
                const QString entryPath = QString::fromUtf8(path);
                if (isImageName(entryPath)) {
                    images.append(entryPath);
                }
            }
            archive_read_data_skip(reader);
        }
    }
    else {
        const char* archiveError = archive_error_string(reader);
        const QString error = archiveError == nullptr ? QString() : QString::fromUtf8(archiveError);
        archive_read_free(reader);
        return std::unexpected(error.isEmpty() ? QStringLiteral("Failed to open archive.") : error);
    }

    archive_read_free(reader);
#else
    Q_UNUSED(archivePath)
    return std::unexpected(QStringLiteral("LibArchive support is not available in this build."));
#endif

    images.sort(Qt::CaseInsensitive);
    return images;
}

} // namespace labelqt::services
