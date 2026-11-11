#include "WorkspaceStorageService.h"
#include "TransferSupport.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
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
    try {
        name = name.trimmed();
        require(!name.isEmpty(), tr("Enter a valid ZIP file name without folders or reserved characters."));
        if (!name.endsWith(".zip", Qt::CaseInsensitive))
            name += ".zip";
        require(!name.contains('/') && name.size() > 4,
                tr("Enter a valid ZIP file name without folders or reserved characters."));
        relativePath(name, {});
        return name;
    }
    catch (const std::exception&) {
        return std::unexpected(tr("Enter a valid ZIP file name without folders or reserved characters."));
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
