#include "services/io/FileRevision.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace labelqt::services {

bool FileRevision::sameContent(const FileRevision& other) const
{
    return exists == other.exists && sha256 == other.sha256;
}

std::expected<FileRevision, QString> FileRevision::read(const QString& path)
{
    const QFileInfo info(path);
    if ((info.exists() && !info.isFile()) || (info.isSymLink() && !info.exists()))
        return std::unexpected(QCoreApplication::translate("AtomicFileWriter", "Not a regular file: %1").arg(path));
    if (!info.exists())
        return FileRevision{};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError)
        return std::unexpected(file.errorString());
    return FileRevision{true, hash.result(), info.lastModified()};
}

} // namespace labelqt::services
