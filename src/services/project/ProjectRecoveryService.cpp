#include "services/project/ProjectRecoveryService.h"
#include "services/io/ProjectFileTransaction.h"

#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/FileRenameTransaction.h"
#include "services/io/FileRevision.h"
#include "services/project/ProjectLocationService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <exception>

namespace labelqt::services {

QString ProjectRecoveryService::backupDirectory(const QString& projectPath,
                                                const labelqt::core::AppPreferences& preferences)
{
    return QDir(QFileInfo(projectPath).absolutePath())
        .absoluteFilePath(preferences.backupPath().isEmpty() ? QStringLiteral("bak") : preferences.backupPath());
}

std::expected<void, QString> ProjectRecoveryService::writeBackup(const labelqt::core::Project& project,
                                                                 const QString& path, bool automatic)
{
    try {
        const auto output = ProjectLocationService::forOutput(project, path);
        const auto bytes = labelqt::core::LabelPlusDocument::serialize(output).toUtf8();
        const auto written = labelqt::core::AtomicFileWriter::writeNew(path, bytes);
        if (!written)
            return written;
        const QJsonObject record{
            {"version", 1},
            {"automatic", automatic},
            {"project", QFileInfo(project.filePath()).absoluteFilePath()},
            {"snapshot", QFileInfo(path).absoluteFilePath()},
            {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
        return labelqt::core::AtomicFileWriter::writeNew(path + QStringLiteral(".labelqt-backup.json"),
                                                         QJsonDocument(record).toJson());
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<QString, QString>
ProjectRecoveryService::preserveLocalEdits(const labelqt::core::Project& project,
                                           const labelqt::core::AppPreferences& preferences)
{
    const QDir directory(backupDirectory(project.filePath(), preferences));
    if (!QDir().mkpath(directory.absolutePath()))
        return std::unexpected(
            QCoreApplication::translate("ProjectRecoveryService", "Could not create backup directory: %1")
                .arg(directory.absolutePath()));
    const auto path = directory.filePath(
        QStringLiteral("before-reload-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto written = writeBackup(project, path);
    if (!written)
        return std::unexpected(written.error());
    return path;
}

QVector<ProjectRecoveryEntry> ProjectRecoveryService::entries(const QString& projectPath,
                                                              const labelqt::core::AppPreferences& preferences)
{
    QVector<ProjectRecoveryEntry> result;
    if (projectPath.isEmpty())
        return result;
    const auto project = QFileInfo(projectPath).absoluteFilePath();
    const auto previousPath = ProjectFileTransaction::previousVersionPath(projectPath);
    const auto previous = FileRevision::read(previousPath);
    if (previous && previous->exists)
        result.append({ProjectRecoveryEntry::Kind::Backup, previousPath, project, previousPath, previous->modified,
                       previous->sha256, false});
    const auto append = [&](const QFileInfo& info, ProjectRecoveryEntry::Kind kind) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return;
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        const auto owner = object.value("project").toString();
        if (!owner.isEmpty() && QFileInfo(owner).absoluteFilePath() != project)
            return;
        if (kind == ProjectRecoveryEntry::Kind::Backup && (owner.isEmpty() || object.value("version").toInt() != 1))
            return;
        result.append(
            {kind, info.absoluteFilePath(), owner.isEmpty() ? project : owner, object.value("snapshot").toString(),
             QDateTime::fromString(object.value("created").toString(), Qt::ISODateWithMs),
             QByteArray::fromHex(object.value("sha256").toString().toLatin1()), object.value("automatic").toBool()});
        if (!result.last().created.isValid())
            result.last().created = info.lastModified();
    };
    for (const auto& info : QDir(backupDirectory(projectPath, preferences))
                                .entryInfoList({QStringLiteral("*.labelqt-backup.json")}, QDir::Files))
        append(info, ProjectRecoveryEntry::Kind::Backup);
    for (const auto& info : QFileInfo(projectPath)
                                .dir()
                                .entryInfoList({QStringLiteral(".labelqt-rename-*.json")}, QDir::Files | QDir::Hidden))
        append(info, ProjectRecoveryEntry::Kind::Rename);
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) { return left.created > right.created; });
    return result;
}

std::expected<labelqt::core::Project, QString> ProjectRecoveryService::preview(const ProjectRecoveryEntry& entry)
{
    if (entry.kind != ProjectRecoveryEntry::Kind::Backup)
        return std::unexpected(
            QCoreApplication::translate("ProjectRecoveryService", "This record requires file recovery."));
    const auto revision = FileRevision::read(entry.snapshotPath);
    if (!revision || !revision->exists || entry.digest.size() != 32 || revision->sha256 != entry.digest)
        return std::unexpected(QCoreApplication::translate("ProjectRecoveryService", "The backup cannot be verified."));
    try {
        auto loaded = labelqt::core::LabelPlusDocument::readFromFile(entry.snapshotPath);
        const auto after = FileRevision::read(entry.snapshotPath);
        if (!after || !after->sameContent(*revision) || loaded.sha256 != entry.digest)
            return std::unexpected(
                QCoreApplication::translate("ProjectRecoveryService", "The backup cannot be verified."));
        return std::move(loaded.project);
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<void, QString> ProjectRecoveryService::restoreCopy(const ProjectRecoveryEntry& entry,
                                                                 const QString& target)
{
    if (QFileInfo::exists(target) ||
        QFileInfo(target).absoluteFilePath() == QFileInfo(entry.projectPath).absoluteFilePath())
        return std::unexpected(
            QCoreApplication::translate("ProjectRecoveryService", "Choose a new file for the recovered copy."));
    const auto snapshot = preview(entry);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    try {
        const auto output = ProjectLocationService::forOutput(*snapshot, target);
        return labelqt::core::AtomicFileWriter::writeNew(target,
                                                         labelqt::core::LabelPlusDocument::serialize(output).toUtf8());
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

bool ProjectRecoveryService::hasNewerRecovery(const QString& projectPath,
                                              const labelqt::core::AppPreferences& preferences)
{
    const QFileInfo original(projectPath);
    const auto disk = FileRevision::read(projectPath);
    for (const auto& entry : entries(projectPath, preferences)) {
        if (entry.kind == ProjectRecoveryEntry::Kind::Rename)
            return true;
        if (!original.exists() || entry.created > original.lastModified()) {
            const auto snapshot = preview(entry);
            if (!snapshot)
                continue;
            const auto output = ProjectLocationService::forOutput(*snapshot, projectPath);
            const auto restoredDigest = QCryptographicHash::hash(
                labelqt::core::LabelPlusDocument::serialize(output).toUtf8(), QCryptographicHash::Sha256);
            if (!disk || restoredDigest != disk->sha256)
                return true;
        }
    }
    return false;
}

void ProjectRecoveryService::pruneAutomaticBackups(const QString& projectPath,
                                                   const labelqt::core::AppPreferences& preferences)
{
    const int maximum = preferences.backupMaximumCount();
    if (maximum <= 0 || !FileRenameTransaction::checkRecoveryDirectory(QFileInfo(projectPath).absolutePath()))
        return;
    const auto directory = QDir(backupDirectory(projectPath, preferences)).canonicalPath();
    int retained = 0;
    for (const auto& entry : entries(projectPath, preferences)) {
        if (entry.kind != ProjectRecoveryEntry::Kind::Backup || !entry.automatic)
            continue;
        if (++retained <= maximum)
            continue;
        const QFileInfo snapshot(entry.snapshotPath), record(entry.recordPath);
        if (snapshot.isSymLink() || record.isSymLink() || !snapshot.isFile() ||
            snapshot.dir().canonicalPath() != directory || record.dir().canonicalPath() != directory ||
            entry.recordPath != entry.snapshotPath + QStringLiteral(".labelqt-backup.json"))
            continue;
        const auto revision = FileRevision::read(entry.snapshotPath);
        if (!revision || revision->sha256 != entry.digest)
            continue;
        if (QFile::remove(entry.snapshotPath))
            QFile::remove(entry.recordPath);
    }
}

std::expected<void, QString> ProjectRecoveryService::recoverFiles(const QString& recordPath, bool complete)
{
    try {
        const auto unsafe = []() -> std::expected<void, QString> {
            return std::unexpected(QCoreApplication::translate(
                "ProjectRecoveryService",
                "Recovery is ambiguous or files have changed. No automatic recovery is possible."));
        };
        QFile file(recordPath);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(file.errorString());
        const auto document = QJsonDocument::fromJson(file.readAll());
        file.close();
        const auto record = document.object();
        if (record.value("version").toInt() != 2 || !record.value("moves").isArray())
            return unsafe();
        const QString project = record.value("project").toString();
        const auto before = QByteArray::fromBase64(record.value("before").toString().toLatin1(),
                                                   QByteArray::AbortOnBase64DecodingErrors);
        const auto after = QByteArray::fromBase64(record.value("after").toString().toLatin1(),
                                                  QByteArray::AbortOnBase64DecodingErrors);
        if (project.isEmpty() || before.isEmpty() || after.isEmpty() ||
            QFileInfo(project).absolutePath() != QFileInfo(recordPath).absolutePath())
            return unsafe();
        ProjectFileTransaction transaction(project);
        const auto revision = FileRevision::read(project);
        if (!revision || !revision->exists ||
            (revision->sha256 != QCryptographicHash::hash(before, QCryptographicHash::Sha256) &&
             revision->sha256 != QCryptographicHash::hash(after, QCryptographicHash::Sha256)))
            return unsafe();
        try {
            labelqt::core::LabelPlusDocument::parse(QString::fromUtf8(before), project);
            labelqt::core::LabelPlusDocument::parse(QString::fromUtf8(after), project);
        }
        catch (const std::exception&) {
            return unsafe();
        }

        struct RecoveryMove {
            QString current, temporary, destination;
            QByteArray digest;
        };
        QVector<RecoveryMove> moves;
        QSet<QString> allPaths, occupied, destinations, temporaryPaths;
        QSet<QByteArray> hashes;
        for (const auto& value : record.value("moves").toArray()) {
            const auto item = value.toObject();
            const QString source = item.value("source").toString();
            const QString target = item.value("target").toString();
            const QString temporary = item.value("temporary").toString();
            const QByteArray hash = QByteArray::fromHex(item.value("sha256").toString().toLatin1());
            if (hash.size() != 32 || hashes.contains(hash) || temporaryPaths.contains(temporary))
                return unsafe();
            hashes.insert(hash);
            temporaryPaths.insert(temporary);
            const QString destination = complete ? target : source;
            if (destinations.contains(destination))
                return unsafe();
            destinations.insert(destination);
            for (const auto& path : {source, target, temporary}) {
                if (!QFileInfo(path).isAbsolute() || QFileInfo(path).isSymLink() || path == project ||
                    path == recordPath)
                    return unsafe();
                allPaths.insert(path);
            }
            moves.append({{}, temporary, destination, hash});
        }
        if (moves.isEmpty())
            return unsafe();
        // Identify each file across all journal paths before performing any write.
        for (const auto& path : std::as_const(allPaths)) {
            const auto state = FileRevision::read(path);
            if (!state)
                return unsafe();
            if (!state->exists)
                continue;
            auto found = std::find_if(moves.begin(), moves.end(),
                                      [&](const auto& move) { return move.digest == state->sha256; });
            if (found == moves.end() || !found->current.isEmpty())
                return unsafe();
            found->current = path;
            occupied.insert(path);
        }
        for (const auto& move : std::as_const(moves)) {
            if (move.current.isEmpty() || (occupied.contains(move.temporary) && move.current != move.temporary))
                return unsafe();
        }
        for (auto& move : moves) {
            if (move.current != move.temporary) {
                if (!QFile::rename(move.current, move.temporary))
                    return std::unexpected(QCoreApplication::translate(
                        "ProjectRecoveryService", "Recovery stopped. Keep the recovery record and temporary files."));
                move.current = move.temporary;
            }
        }
        for (const auto& move : std::as_const(moves)) {
            if (!QFile::rename(move.temporary, move.destination))
                return std::unexpected(QCoreApplication::translate(
                    "ProjectRecoveryService", "Recovery stopped. Keep the recovery record and temporary files."));
        }
        (void)transaction.write(complete ? after : before, *revision);
        if (!QFile::remove(recordPath))
            return std::unexpected(QCoreApplication::translate(
                "ProjectRecoveryService", "Files recovered, but the recovery record could not be removed."));
        return {};
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

} // namespace labelqt::services
