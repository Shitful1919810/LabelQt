#include "core/AtomicFileWriter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSaveFile>

namespace labelqt::core {

std::expected<void, QString> AtomicFileWriter::write(const QString& path, const QByteArray& bytes)
{
    const QFileInfo info(path);
    if (info.exists() && !info.isFile()) {
        return std::unexpected(QCoreApplication::translate("AtomicFileWriter", "Not a regular file: %1").arg(path));
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(file.errorString());
    }
    if (file.write(bytes) != bytes.size()) {
        return std::unexpected(file.errorString());
    }
    if (!file.commit()) {
        return std::unexpected(file.errorString());
    }
    return {};
}

} // namespace labelqt::core
