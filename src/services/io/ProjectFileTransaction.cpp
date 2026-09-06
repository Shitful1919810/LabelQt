#include "services/io/ProjectFileTransaction.h"
#include "core/AtomicFileWriter.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <utility>

namespace labelqt::services {
namespace {
QString canonicalTarget(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists())
        return info.canonicalFilePath();
    const auto parent = info.dir().canonicalPath();
    return parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName());
}

FileRevision checkedRevision(const QString& path, const FileRevision& expected)
{
    const auto current = FileRevision::read(path);
    if (!current)
        throw std::runtime_error(current.error().toStdString());
    if (!current->sameContent(expected))
        throw ProjectFileConflict(*current);
    return *current;
}
} // namespace

ProjectFileConflict::ProjectFileConflict(FileRevision diskRevision)
    : std::runtime_error(
          QCoreApplication::translate("ProjectController", "The project was changed on disk.").toStdString()),
      revision(std::move(diskRevision))
{
}

QString ProjectFileTransaction::lockPath(const QString& path)
{
    return canonicalTarget(path) + QStringLiteral(".labelqt-write.lock");
}

QString ProjectFileTransaction::previousVersionPath(const QString& path)
{
    return canonicalTarget(path) + QStringLiteral(".labelqt-previous");
}

ProjectFileTransaction::ProjectFileTransaction(const QString& path)
    : m_path(canonicalTarget(path)), m_lock(lockPath(path))
{
    const QFileInfo info(path);
    if (path.isEmpty() || (info.exists() && !info.isFile()) || (info.isSymLink() && !info.exists()))
        throw std::runtime_error(
            QCoreApplication::translate("AtomicFileWriter", "Not a regular file: %1").arg(path).toStdString());
    if (!m_lock.tryLock(0))
        throw std::runtime_error(
            QCoreApplication::translate("ProjectFileTransaction", "Could not lock the project for writing: %1")
                .arg(path)
                .toStdString());
}

FileRevision ProjectFileTransaction::write(const QByteArray& bytes, const FileRevision& expected)
{
    checkedRevision(m_path, expected);
    if (!expected.exists) {
        const auto result = labelqt::core::AtomicFileWriter::writeNew(m_path, bytes);
        if (!result)
            throw std::runtime_error(result.error().toStdString());
    }
    else {
        QSaveFile output(m_path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.flush())
            throw std::runtime_error(output.errorString().toStdString());

        // Keep the previous disk version independently of autosave settings. Validate what was copied.
        const auto backupPath = previousVersionPath(m_path);
        if (QFileInfo(backupPath).isSymLink())
            throw std::runtime_error(QCoreApplication::translate("AtomicFileWriter", "Not a regular file: %1")
                                         .arg(backupPath)
                                         .toStdString());
        QFile input(m_path);
        QSaveFile backup(backupPath);
        backup.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly) || !backup.open(QIODevice::WriteOnly))
            throw std::runtime_error((!input.isOpen() ? input.errorString() : backup.errorString()).toStdString());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const auto block = input.read(65536);
            if (input.error() != QFileDevice::NoError || (block.isEmpty() && !input.atEnd()))
                throw std::runtime_error(input.errorString().toStdString());
            hash.addData(block);
            if (backup.write(block) != block.size())
                throw std::runtime_error(backup.errorString().toStdString());
        }
        input.close();
        if (hash.result() != expected.sha256)
            throw ProjectFileConflict({true, hash.result(), {}});
        if (!backup.commit())
            throw std::runtime_error(backup.errorString().toStdString());
        checkedRevision(m_path, expected);
        if (!output.commit())
            throw std::runtime_error(output.errorString().toStdString());
    }
    return {true, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256), QFileInfo(m_path).lastModified()};
}

} // namespace labelqt::services
