#pragma once

#include "services/io/FileRevision.h"
#include <QLockFile>
#include <stdexcept>

namespace labelqt::services {

class ProjectFileConflict final : public std::runtime_error {
public:
    explicit ProjectFileConflict(FileRevision diskRevision);
    const FileRevision revision;
};

// Hold for the complete workflow, including image renames and their rollback.
class ProjectFileTransaction final {
public:
    explicit ProjectFileTransaction(const QString& path);
    FileRevision write(const QByteArray& bytes, const FileRevision& expected);
    static QString lockPath(const QString& path);
    static QString previousVersionPath(const QString& path);

private:
    QString m_path;
    QLockFile m_lock;
};

} // namespace labelqt::services
