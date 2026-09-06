#include "core/AtomicFileWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>

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

std::expected<void, QString> AtomicFileWriter::writeNew(const QString& path, const QByteArray& bytes)
{
    QTemporaryFile temporary(QFileInfo(path).dir().filePath(QStringLiteral(".labelqt-copy-XXXXXX")));
    if (!temporary.open() || temporary.write(bytes) != bytes.size() || !temporary.flush())
        return std::unexpected(temporary.errorString());
    temporary.close();
    // QFile::rename refuses an existing destination, including one created after the dialog closed.
    QFile staged(temporary.fileName());
    if (!staged.rename(path))
        return std::unexpected(staged.errorString());
    temporary.setAutoRemove(false);
    return {};
}

} // namespace labelqt::core
