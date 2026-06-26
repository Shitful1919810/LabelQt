#pragma once

#include <expected>

#include <QString>
#include <QStringList>

namespace labelqt::services {

class ArchiveReader {
public:
    static bool isAvailable() noexcept;
    std::expected<QStringList, QString> tryListImages(const QString& archivePath) const;
    QStringList listImages(const QString& archivePath) const;
};

} // namespace labelqt::services
