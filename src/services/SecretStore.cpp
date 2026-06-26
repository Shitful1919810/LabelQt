#include "services/SecretStore.h"

#include <qt6keychain/keychain.h>

#include <QEventLoop>

#include <expected>
#include <memory>
#include <optional>

namespace labelqt::services {

namespace {
template <typename Job>
void runJob(Job* job)
{
    QEventLoop loop;
    job->setAutoDelete(false);
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->start();
    loop.exec();
}

QString effectiveService(const QString& service)
{
    const QString trimmed = service.trimmed();
    return trimmed.isEmpty() ? SecretStore::defaultService() : trimmed;
}
} // namespace

QString SecretStore::defaultService()
{
    return QStringLiteral("LabelQt");
}

SecretStoreReadResult SecretStore::readText(const QString& service, const QString& account)
{
    const std::expected<std::optional<QString>, QString> value = tryReadText(service, account);

    SecretStoreReadResult result;
    if (!value.has_value()) {
        result.error = value.error();
        return result;
    }
    if (value->has_value()) {
        result.found = true;
        result.value = **value;
    }
    return result;
}

std::expected<std::optional<QString>, QString> SecretStore::tryReadText(const QString& service, const QString& account)
{
    auto job = std::make_unique<QKeychain::ReadPasswordJob>(effectiveService(service));
    job->setKey(account);
    runJob(job.get());

    if (job->error() == QKeychain::NoError) {
        return job->textData();
    }
    if (job->error() == QKeychain::EntryNotFound) {
        return std::optional<QString>{};
    }
    return std::unexpected(job->errorString());
}

std::expected<void, QString> SecretStore::tryWriteText(const QString& service, const QString& account,
                                                       const QString& value)
{
    auto job = std::make_unique<QKeychain::WritePasswordJob>(effectiveService(service));
    job->setKey(account);
    job->setTextData(value);
    runJob(job.get());

    if (job->error() == QKeychain::NoError) {
        return {};
    }
    return std::unexpected(job->errorString());
}

SecretStoreWriteResult SecretStore::writeText(const QString& service, const QString& account, const QString& value)
{
    const std::expected<void, QString> writeResult = tryWriteText(service, account, value);

    SecretStoreWriteResult result;
    result.success = writeResult.has_value();
    if (!result.success) {
        result.error = writeResult.error();
    }
    return result;
}

} // namespace labelqt::services
