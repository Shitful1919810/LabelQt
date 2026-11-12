#include "WorkspaceStorageService.h"
#include "TransferSupport.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace labelqt::services {
using namespace transfer;
namespace {
bool validId(const QString& id)
{
    return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}

QString workspacePath(const QString& root, const QString& id)
{
    require(validId(id), WorkspaceStorageService::tr("The local project is unavailable."));
    const auto path = QDir(privateRoot(root, false)).filePath("workspaces/" + id);
    noLinks(path);
    return path;
}

bool disposableDirectory(const QFileInfo& entry, const QDateTime& cutoff)
{
    return entry.isDir() && !entry.isSymLink() && entry.lastModified() < cutoff;
}

std::expected<QString, QString> outputName(QString name, const QString& suffix, const QString& error)
{
    try {
        name = name.trimmed();
        require(!name.isEmpty(), error);
        if (!name.endsWith(suffix, Qt::CaseInsensitive))
            name += suffix;
        require(!name.contains('/') && name.size() > suffix.size(), error);
        relativePath(name, {});
        return name;
    }
    catch (const std::exception&) {
        return std::unexpected(error);
    }
}
} // namespace

namespace transfer {
QString privateRoot(const QString& root, bool create)
{
    require(!root.isEmpty() && QDir::isAbsolutePath(root),
            ProjectTransferService::tr("An absolute app-owned transfer root is required."));
    noLinks(root);
    if (create)
        makeDirectory(root);
    require(QFileInfo(root).isDir(), ProjectTransferService::tr("The transfer root is unavailable."));
    return QDir::cleanPath(root);
}

WorkspaceInfo readWorkspaceInfo(const QString& directory)
{
    noLinks(directory);
    const auto id = QFileInfo(directory).fileName();
    const auto error = WorkspaceStorageService::tr("The local project is unavailable.");
    require(validId(id), error);
    const auto document = QJsonDocument::fromJson(readBounded(QDir(directory).filePath("workspace.json"), 8192));
    require(document.isObject(), error);
    const auto object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it)
        require(QStringList{"version", "id", "title", "projectPath", "archiveName"}.contains(it.key()), error);
    require(object.value("version") == QJsonValue(1) && object.value("id").toString() == id &&
                object.value("title").isString() && !object.value("title").toString().isEmpty() &&
                object.value("projectPath").toString() == "project/project.txt" &&
                (!object.contains("archiveName") || object.value("archiveName").isString()),
            error);
    const auto path = QDir(directory).filePath("project/project.txt");
    noLinks(path);
    require(QFileInfo(path).isFile(), error);
    const auto title = object.value("title").toString();
    const auto name = object.value("archiveName").toString();
    return {id, title, path, WorkspaceStorageService::suggestedArchiveName(name.isEmpty() ? title : name),
            QFileInfo(path).lastModified()};
}

QByteArray workspaceManifest(const WorkspaceInfo& info)
{
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
                                                 {"id", info.id},
                                                 {"title", info.title},
                                                 {"projectPath", "project/project.txt"},
                                                 {"archiveName", info.archiveName}})
                           .toJson(QJsonDocument::Compact);
    require(bytes.size() <= 8192, ProjectTransferService::tr("Workspace title is too long."));
    return bytes;
}

qint64 shareCacheSize(const QString& root)
{
    const auto outgoing = QDir(privateRoot(root, true)).filePath("outgoing");
    noLinks(outgoing);
    qint64 size = 0;
    QDirIterator it(outgoing, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto bytes = it.fileInfo().size();
        if (bytes > WorkspaceStorageService::shareCacheLimit - size)
            return WorkspaceStorageService::shareCacheLimit;
        size += bytes;
    }
    return size;
}
} // namespace transfer

std::expected<QString, QString> WorkspaceStorageService::archiveFileName(QString name)
{
    return outputName(std::move(name), ".zip", tr("Enter a valid ZIP file name without folders or reserved characters."));
}

std::expected<QString, QString> WorkspaceStorageService::pdfFileName(QString name)
{
    return outputName(std::move(name), ".pdf", tr("Enter a valid PDF file name without folders or reserved characters."));
}

std::expected<QString, QString> WorkspaceStorageService::validateSharedFile(const QString& path) const
{
    const auto error = tr("The last export is no longer available. Export the project or report again.");
    try {
        const auto outgoing = QDir(privateRoot(m_root, false)).filePath("outgoing");
        const QFileInfo file(path);
        const QDir batch = file.dir();
        require(file.isAbsolute() && QFileInfo(batch.absolutePath()).dir().absolutePath() == outgoing &&
                    validId(batch.dirName()),
                error);
        noLinks(path);
        require(file.isFile() && file.isReadable() && file.size() > 0 &&
                    file.canonicalFilePath() == file.absoluteFilePath() &&
                    (file.suffix().compare("zip", Qt::CaseInsensitive) == 0 ||
                     file.suffix().compare("pdf", Qt::CaseInsensitive) == 0),
                error);
        return file.absoluteFilePath();
    }
    catch (const std::exception&) {
        return std::unexpected(error);
    }
}

std::expected<QString, QString> WorkspaceStorageService::createSharedFile(
    const QString& fileName, const std::function<std::expected<void, QString>(const QString&)>& write,
    const std::function<bool()>& canceled, const std::function<qint64()>& estimatePeakBytes) const
{
    try {
        const auto name =
            fileName.endsWith(".pdf", Qt::CaseInsensitive) ? pdfFileName(fileName) : archiveFileName(fileName);
        require(name.has_value(), name ? QString{} : name.error());
        const auto root = privateRoot(m_root, true);
        const auto cacheFull = ProjectTransferService::tr(
            "The share cache is full (2 GiB). Exports are retained for seven days; try again after they expire.");
        require(!canceled || !canceled(), tr("Export canceled."));
        require(shareCacheSize(root) < shareCacheLimit, cacheFull);
        const auto checkSpace = [&](qint64 estimate) {
            const QStorageInfo storage(root);
            const qint64 available = m_availableBytes ? m_availableBytes(root)
                                     : storage.isValid() && storage.isReady() && !storage.isReadOnly()
                                         ? storage.bytesAvailable()
                                         : -1;
            require(available >= 0, tr("Cannot determine available storage for export."));
            require(estimate >= 0 && available >= saveSpaceReserve && estimate <= available - saveSpaceReserve,
                    tr("Not enough storage for export staging while keeping 256 MiB available for project saves."));
        };
        checkSpace(0);
        checkSpace(estimatePeakBytes ? estimatePeakBytes() : shareCacheLimit);
        const auto outgoing = QDir(root).filePath("outgoing");
        makeDirectory(outgoing);
        QTemporaryDir stage(QDir(root).filePath(".staging-export-XXXXXX"));
        require(stage.isValid(), tr("Cannot create report staging directory."));
        const auto filePath = stage.filePath("payload");
        const auto result = write(filePath);
        require(result.has_value(), result ? QString{} : result.error());
        require(!canceled || !canceled(), tr("Export canceled."));
        noLinks(filePath);
        require(QFileInfo(filePath).isFile() && QFileInfo(filePath).size() > 0, tr("The exported file is empty."));
        require(QFileInfo(filePath).size() <= shareCacheLimit - shareCacheSize(root), cacheFull);
        checkSpace(0);
        require(QFile::setPermissions(filePath, QFileDevice::ReadOwner), tr("Cannot protect the exported file."));
        const auto candidate = stage.filePath("share");
        makeDirectory(candidate);
        require(QDir().rename(filePath, QDir(candidate).filePath(*name)), tr("Cannot publish the exported file."));
        const auto output = QDir(outgoing).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        require(QDir().rename(candidate, output), tr("Cannot publish the exported file."));
        return QDir(output).filePath(*name);
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

QString WorkspaceStorageService::suggestedArchiveName(QString name)
{
    name = QFileInfo(name).fileName();
    const auto valid = archiveFileName(name);
    return valid ? *valid : QStringLiteral("project.zip");
}

std::expected<WorkspaceInfo, QString> WorkspaceStorageService::rename(const QString& id, const QString& title) const
{
    try {
        const auto name = title.trimmed();
        require(!name.isEmpty() && name.size() <= 200 &&
                    std::none_of(name.begin(), name.end(),
                                 [](QChar ch) { return ch.isNull() || ch.category() == QChar::Other_Control; }),
                tr("Enter a project name of 1 to 200 characters."));
        const auto directory = workspacePath(m_root, id);
        auto info = readWorkspaceInfo(directory);
        info.title = name;
        const auto bytes = workspaceManifest(info);
        QSaveFile file(QDir(directory).filePath("workspace.json"));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
                tr("The project name could not be saved."));
        return info;
    }
    catch (const std::exception& e) {
        return std::unexpected(QString::fromUtf8(e.what()));
    }
}

std::expected<void, QString> WorkspaceStorageService::remove(const QString& id) const
{
    try {
        const auto source = workspacePath(m_root, id);
        (void)readWorkspaceInfo(source);
        const auto destination =
            QDir(m_root).filePath(".deleted-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        require(QDir().rename(source, destination), tr("The local project could not be deleted."));
        return {};
    }
    catch (const std::exception& e) {
        return std::unexpected(QString::fromUtf8(e.what()));
    }
}

std::expected<void, QString> WorkspaceStorageService::cleanDisposableFiles(QDateTime now) const
{
    try {
        const auto root = privateRoot(m_root, true);
        bool ok = true;
        const QRegularExpression staging(QStringLiteral("^\\.staging-(import|export)-[A-Za-z0-9]{6}$"));
        for (const auto& entry :
             QDir(root).entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            const auto name = entry.fileName();
            const bool deleted = name.startsWith(".deleted-") && validId(name.mid(9));
            if (deleted || (staging.match(name).hasMatch() && disposableDirectory(entry, now.addDays(-1))))
                ok = QDir(entry.absoluteFilePath()).removeRecursively() && ok;
        }
        const auto outgoing = QDir(root).filePath("outgoing");
        noLinks(outgoing);
        for (const auto& entry :
             QDir(outgoing).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            if (validId(entry.fileName()) && disposableDirectory(entry, now.addDays(-7)))
                ok = QDir(entry.absoluteFilePath()).removeRecursively() && ok;
            else if (entry.isFile() && entry.suffix() == "zip" && validId(entry.completeBaseName()) &&
                     entry.lastModified() < now.addDays(-7))
                ok = QFile::remove(entry.absoluteFilePath()) && ok;
        }
        require(ok, tr("Some temporary files could not be removed. Cleanup will be retried later."));
        return {};
    }
    catch (const std::exception& e) {
        return std::unexpected(QString::fromUtf8(e.what()));
    }
}
} // namespace labelqt::services
