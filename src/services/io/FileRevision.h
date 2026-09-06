#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <expected>

namespace labelqt::services {

struct FileRevision {
    bool exists{false};
    QByteArray sha256;
    QDateTime modified;

    bool sameContent(const FileRevision& other) const;
    static std::expected<FileRevision, QString> read(const QString& path);
};

} // namespace labelqt::services
