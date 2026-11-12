#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QStringList>
#include <expected>
#include <memory>
#include <vector>

namespace labelqt::services {

// Acquire after the project write lock; retain through TXT commit or file rollback.
class RenameDirectoryLocks final {
public:
    std::expected<void, QString> acquire(const QStringList& directories)
    {
        QStringList paths;
        for (const auto& directory : directories) {
            const auto canonical = QDir(directory).canonicalPath();
            if (canonical.isEmpty())
                return std::unexpected(lockError(directory));
            paths.append(QDir(canonical).filePath(QStringLiteral(".labelqt-rename.lock")));
        }
        paths.removeDuplicates();
        paths.sort();
        std::vector<std::unique_ptr<QLockFile>> locks;
        for (const auto& path : paths) {
            auto lock = std::make_unique<QLockFile>(path);
            if (!lock->tryLock(0))
                return std::unexpected(lockError(path));
            locks.push_back(std::move(lock));
        }
        m_locks = std::move(locks);
        return {};
    }

    void release()
    {
        m_locks.clear();
    }

private:
    static QString lockError(const QString& path)
    {
        return QCoreApplication::translate("ProjectFileTransaction", "Could not lock the project for writing: %1")
            .arg(path);
    }

    std::vector<std::unique_ptr<QLockFile>> m_locks;
};

} // namespace labelqt::services
