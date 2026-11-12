#include "services/transfer/TransferSupport.h"

#include "core/LabelPlusDocument.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace labelqt::services::transfer {
QString ioError(const QString& path)
{
    return ProjectTransferService::tr("Transfer could not read or write a regular local file: %1").arg(path);
}

namespace {
constexpr qint64 chunkSize = 65536;

QByteArray digestFile(const QString& path, qint64 limit)
{
    noLinks(path);
    QFile file(path);
    require(QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly), ioError(path));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!file.atEnd()) {
        const auto bytes = file.read(chunkSize);
        require(file.error() == QFileDevice::NoError && !bytes.isEmpty(), ioError(path));
        require(bytes.size() <= limit - count, ProjectTransferService::tr("Transfer size limit exceeded."));
        count += bytes.size();
        hash.addData(bytes);
    }
    return hash.result();
}

} // namespace

void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}
void require(bool condition, const QString& message)
{
    if (!condition)
        fail(message);
}

void checkLimits(const TransferLimits& limits)
{
    require(limits.inputBytes > 0 && limits.fileBytes > 0 && limits.totalBytes > 0 && limits.textBytes > 0 &&
                limits.entries > 0 && limits.depth > 0 && limits.depth <= 32 && limits.entries <= 10000 &&
                limits.inputBytes <= 2LL * 1024 * 1024 * 1024 && limits.fileBytes <= 512LL * 1024 * 1024 &&
                limits.totalBytes <= 8LL * 1024 * 1024 * 1024 && limits.textBytes <= 32LL * 1024 * 1024 &&
                limits.projectLabels > 0 && limits.projectLabels <= TransferLimits{}.projectLabels &&
                limits.pageLabels > 0 && limits.pageLabels <= TransferLimits{}.pageLabels,
            ProjectTransferService::tr("Invalid transfer limits."));
}

QString relativePath(const QString& path, const TransferLimits& limits)
{
    const auto error = ProjectTransferService::tr("Unsafe or nonportable transfer path: %1").arg(path);
    require(!path.isEmpty() && path.toUtf8().size() <= 4096 && path == path.trimmed() &&
                QString::fromUtf8(path.toUtf8()) == path,
            error);
    const auto parts = path.split('/');
    require(parts.size() <= limits.depth, error);
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const auto& part : parts) {
        require(!part.isEmpty() && part != "." && part != ".." && part.toUtf8().size() <= 255 && !part.endsWith('.') &&
                    !part.endsWith(' ') && !device.match(part).hasMatch(),
                error);
        for (const auto ch : part)
            require(ch.unicode() >= 32 && ch.unicode() != 127 && !QStringLiteral("\\:<>\"|?*").contains(ch) &&
                        ch.category() != QChar::Other_Format,
                    error);
    }
    return path;
}

void noLinks(const QString& path)
{
    require(QDir::isAbsolutePath(path) && !path.contains(QChar::Null) && !path.split('/').contains(".."),
            ioError(path));
    QString current = QDir::cleanPath(path);
    for (;;) {
        const QFileInfo info(current);
        require(!info.isSymLink(), ProjectTransferService::tr("Links are not allowed in transfer paths: %1").arg(path));
        const auto parent = info.absolutePath();
        if (parent == current)
            break;
        current = parent;
    }
}

void makeDirectory(const QString& path)
{
    noLinks(path);
    require(QDir().mkpath(path), ioError(path));
    noLinks(path);
}

QByteArray readBounded(const QString& path, qint64 limit)
{
    noLinks(path);
    QFile file(path);
    require(QFileInfo(path).isFile() && file.size() <= limit && file.open(QIODevice::ReadOnly), ioError(path));
    const auto bytes = file.read(limit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= limit, ioError(path));
    return bytes;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    makeDirectory(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush(),
            ioError(path));
}

QByteArray copyFile(const QString& from, const QString& to, qint64 limit, qint64& remaining)
{
    noLinks(from);
    const QFileInfo before(from);
    require(before.isFile() && before.size() <= limit && before.size() <= remaining, ioError(from));
    QFile input(from);
    require(input.open(QIODevice::ReadOnly), ioError(from));
    makeDirectory(QFileInfo(to).absolutePath());
    QFile output(to);
    require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), ioError(to));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!input.atEnd()) {
        const auto bytes = input.read(chunkSize);
        require(input.error() == QFileDevice::NoError && !bytes.isEmpty(), ioError(from));
        require(bytes.size() <= limit - count && bytes.size() <= remaining,
                ProjectTransferService::tr("Transfer size limit exceeded."));
        count += bytes.size();
        remaining -= bytes.size();
        require(output.write(bytes) == bytes.size(), ioError(to));
        hash.addData(bytes);
    }
    require(output.flush(), ioError(to));
    const QFileInfo after(from);
    const auto digest = hash.result();
    require(count == before.size() && after.size() == before.size() && after.lastModified() == before.lastModified() &&
                digestFile(from, limit) == digest,
            ProjectTransferService::tr("A transfer source changed while being copied. Try again."));
    return digest;
}

core::Project readProject(const QString& path, const TransferLimits& limits)
{
    const auto bytes = readBounded(path, limits.textBytes);
    // Retain the bounded byte snapshot; encoding/format rules belong to the shared reader.
    return core::LabelPlusDocument::readFromBytes(bytes, path).project;
}

void Names::add(const QString& path, bool directory)
{
    const auto parts = path.split('/');
    QString spelling;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (i)
            spelling += '/';
        spelling += parts[i];
        const bool last = i + 1 == parts.size(), isDir = !last || directory;
        const auto key = spelling.normalized(QString::NormalizationForm_C).toCaseFolded();
        auto it = m_names.find(key);
        if (it != m_names.end()) {
            require(it->spelling == spelling && it->directory && isDir && !(last && it->explicitEntry),
                    ProjectTransferService::tr("Duplicate or colliding transfer path: %1").arg(path));
            if (last)
                it->explicitEntry = true;
        }
        else {
            m_names.insert(key, {spelling, isDir, last});
        }
    }
}

} // namespace labelqt::services::transfer
