#pragma once

#include <QByteArray>
#include <QString>

#include <expected>

namespace labelqt::core {

class AtomicFileWriter final {
public:
    static std::expected<void, QString> write(const QString& path, const QByteArray& bytes);
};

} // namespace labelqt::core
