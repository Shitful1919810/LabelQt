#pragma once

#include <QString>
#include <QStringList>

namespace labelqt::services {

class ArchiveReader {
public:
    static bool isAvailable() noexcept;
    QStringList listImages(const QString& archivePath) const;
};

} // namespace labelqt::services
