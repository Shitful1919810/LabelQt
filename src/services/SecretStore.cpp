#include "services/SecretStore.h"

#include <qt6keychain/keychain.h>

#include <QEventLoop>

#include <memory>

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
    auto job = std::make_unique<QKeychain::ReadPasswordJob>(effectiveService(service));
    job->setKey(account);
    runJob(job.get());

    SecretStoreReadResult result;
    if (job->error() == QKeychain::NoError) {
        result.found = true;
        result.value = job->textData();
        return result;
    }
    if (job->error() == QKeychain::EntryNotFound) {
        result.found = false;
        return result;
    }
    result.error = job->errorString();
    return result;
}

SecretStoreWriteResult SecretStore::writeText(const QString& service, const QString& account, const QString& value)
{
    auto job = std::make_unique<QKeychain::WritePasswordJob>(effectiveService(service));
    job->setKey(account);
    job->setTextData(value);
    runJob(job.get());

    SecretStoreWriteResult result;
    result.success = job->error() == QKeychain::NoError;
    if (!result.success) {
        result.error = job->errorString();
    }
    return result;
}

} // namespace labelqt::services
