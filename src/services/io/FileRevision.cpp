#include "services/io/FileRevision.h"

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
