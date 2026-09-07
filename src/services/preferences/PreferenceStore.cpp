#include "services/preferences/PreferenceStore.h"

#include "core/AtomicFileWriter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>

namespace labelqt::services {

PreferenceStore::PreferenceStore(QString path)
{
    const QFileInfo info(path);
    m_path = info.canonicalFilePath();
    if (m_path.isEmpty()) {
        const QString directory = info.absoluteDir().canonicalPath();
        m_path = directory.isEmpty() ? info.absoluteFilePath() : QDir(directory).filePath(info.fileName());
    }
    m_path = QDir::cleanPath(m_path);
}

const QString& PreferenceStore::path() const
{
    return m_path;
}

QString PreferenceStore::lockPath() const
{
    return m_path + QStringLiteral(".lock");
}

bool PreferenceStore::hasBaseline() const
{
    return m_baseline.has_value();
}

std::expected<PreferenceStore::Snapshot, PreferenceStore::Error> PreferenceStore::load()
{
    const QFileInfo info(m_path);
    if ((info.exists() && !info.isFile()) || (info.isSymLink() && !info.exists()))
        return std::unexpected(Error{ErrorCode::ReadFailed, m_path});
    if (!info.exists()) {
        m_baseline = FileRevision{};
        return Snapshot{};
    }

    const auto bytes = BoundedJsonIo::readBytes(m_path, StorageLimits.bytes, m_path);
    if (!bytes)
        return std::unexpected(Error{ErrorCode::ReadFailed, bytes.error()});
    const auto object = BoundedJsonIo::parse(*bytes, StorageLimits, {});
    if (!object)
        return std::unexpected(Error{ErrorCode::InvalidDocument, {}});

    // Hash the very bytes parsed, not a second read that might observe a newer file.
    m_baseline = FileRevision{true, QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256), info.lastModified()};
    return Snapshot{QJsonDocument(*object), true};
}

std::expected<bool, PreferenceStore::Error> PreferenceStore::compareExternalChanges() const
{
    if (!m_baseline)
        return std::unexpected(Error{ErrorCode::MissingBaseline, {}});
    const QFileInfo info(m_path);
    if (!info.exists() && !info.isSymLink())
        return m_baseline->exists;
    const auto bytes = BoundedJsonIo::readBytes(m_path, StorageLimits.bytes, m_path);
    if (!bytes)
        return std::unexpected(Error{ErrorCode::ReadFailed, bytes.error()});
    const FileRevision current{true, QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256), {}};
    return !m_baseline->sameContent(current);
}

std::expected<void, PreferenceStore::Error> PreferenceStore::save(const QJsonDocument& document)
{
    if (!m_baseline)
        return std::unexpected(Error{ErrorCode::MissingBaseline, {}});
    if (!document.isObject())
        return std::unexpected(Error{ErrorCode::InvalidDocument, {}});
    const auto bytes = BoundedJsonIo::encode(document.object(), StorageLimits, {});
    if (!bytes)
        return std::unexpected(Error{ErrorCode::InvalidDocument, {}});
    const QDir directory = QFileInfo(m_path).absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
        return std::unexpected(Error{ErrorCode::WriteFailed, directory.absolutePath()});

    QLockFile lock(lockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return std::unexpected(Error{ErrorCode::Locked, {}});

    // Cooperating writers share this lock through revision validation and atomic commit.
    const auto changed = compareExternalChanges();
    if (!changed)
        return std::unexpected(changed.error());
    if (*changed)
        return std::unexpected(Error{ErrorCode::ExternalChanges, {}});
    const auto written = core::AtomicFileWriter::write(m_path, *bytes);
    if (!written)
        return std::unexpected(Error{ErrorCode::WriteFailed, written.error()});

    m_baseline = FileRevision{true, QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256), {}};
    return {};
}

} // namespace labelqt::services
