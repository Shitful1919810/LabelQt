#pragma once

#include <QString>

namespace labelqt::services {

struct SecretStoreReadResult {
    bool found{false};
    QString value;
    QString error;
};

struct SecretStoreWriteResult {
    bool success{false};
    QString error;
};

class SecretStore final {
public:
    static QString defaultService();
    static SecretStoreReadResult readText(const QString& service, const QString& account);
    static SecretStoreWriteResult writeText(const QString& service, const QString& account, const QString& value);
};

} // namespace labelqt::services
