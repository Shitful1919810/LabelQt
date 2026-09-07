#include "services/project/ProjectRecoveryService.h"
#include "services/io/ProjectFileTransaction.h"

#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/BoundedJsonIo.h"
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
#include <QTextStream>
#include <algorithm>
#include <exception>

namespace labelqt::services {
namespace {
QString invalidBackup()
{
    return QCoreApplication::translate("ProjectRecoveryService", "The backup cannot be verified.");
}

bool validBackupRecord(const QJsonObject& record)
{
    return BoundedJsonIo::isInteger(record.value("version"), 1, 1) && record.value("automatic").isBool() &&
           !record.value("project").toString().isEmpty() && !record.value("snapshot").toString().isEmpty() &&
           QDateTime::fromString(record.value("created").toString(), Qt::ISODateWithMs).isValid() &&
           BoundedJsonIo::isDigest(record.value("sha256"));
}
} // namespace

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
        const QJsonObject record{
            {"version", 1},
            {"automatic", automatic},
            {"project", QFileInfo(project.filePath()).absoluteFilePath()},
            {"snapshot", QFileInfo(path).absoluteFilePath()},
            {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
        const auto recordPath = path + QStringLiteral(".labelqt-backup.json");
        const auto recordBytes = BoundedJsonIo::encode(record, BoundedJsonIo::Backup, invalidBackup());
        if (bytes.size() > BoundedJsonIo::SnapshotBytes || !validBackupRecord(record) || !recordBytes ||
            !BoundedJsonIo::writableFile(path, true) || !BoundedJsonIo::writableFile(recordPath, true))
            return std::unexpected(invalidBackup());
        const auto written = labelqt::core::AtomicFileWriter::writeNew(path, bytes);
        if (!written)
            return written;
        return labelqt::core::AtomicFileWriter::writeNew(recordPath, *recordBytes);
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
    const auto previous = BoundedJsonIo::readBytes(previousPath, BoundedJsonIo::SnapshotBytes, invalidBackup());
    if (previous)
        result.append({ProjectRecoveryEntry::Kind::Backup, previousPath, project, previousPath,
                       QFileInfo(previousPath).lastModified(),
                       QCryptographicHash::hash(*previous, QCryptographicHash::Sha256), false});
    const auto append = [&](const QFileInfo& info, ProjectRecoveryEntry::Kind kind) {
        if (kind == ProjectRecoveryEntry::Kind::Rename) {
            const auto record = recoveryDetails(info.absoluteFilePath());
            if (record && !record->projectPath.isEmpty() &&
                QFileInfo(record->projectPath).absoluteFilePath() != project)
                return;
            // Keep damaged mappings discoverable, but never enable automatic recovery for them.
            result.append({kind, info.absoluteFilePath(), project, {}, info.lastModified(), {}, false});
            return;
        }
        const auto loaded = BoundedJsonIo::read(info.absoluteFilePath(), BoundedJsonIo::Backup, invalidBackup());
        if (!loaded || !validBackupRecord(*loaded))
            return;
        const auto& object = *loaded;
        const auto owner = object.value("project").toString();
        if (!owner.isEmpty() && QFileInfo(owner).absoluteFilePath() != project)
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
                                .entryInfoList({QStringLiteral(".labelqt-rename-*.json")},
                                               QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
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
    const auto bytes = BoundedJsonIo::readBytes(entry.snapshotPath, BoundedJsonIo::SnapshotBytes, invalidBackup());
    if (!bytes || entry.digest.size() != 32 ||
        QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256) != entry.digest)
        return std::unexpected(QCoreApplication::translate("ProjectRecoveryService", "The backup cannot be verified."));
    try {
        QTextStream stream(*bytes);
        stream.setEncoding(QStringConverter::Utf8);
        const auto text = stream.readAll();
        if (stream.status() != QTextStream::Ok)
            return std::unexpected(invalidBackup());
        return labelqt::core::LabelPlusDocument::parse(text, entry.snapshotPath);
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

std::expected<void, QString> ProjectRecoveryService::restoreCopy(const ProjectRecoveryEntry& entry,
                                                                 const QString& target)
{
    if (!BoundedJsonIo::writableFile(target, true) ||
        QFileInfo(target).absoluteFilePath() == QFileInfo(entry.projectPath).absoluteFilePath())
        return std::unexpected(
            QCoreApplication::translate("ProjectRecoveryService", "Choose a new file for the recovered copy."));
    const auto snapshot = preview(entry);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    try {
        const auto output = ProjectLocationService::forOutput(*snapshot, target);
        const auto bytes = labelqt::core::LabelPlusDocument::serialize(output).toUtf8();
        if (bytes.size() > BoundedJsonIo::SnapshotBytes)
            return std::unexpected(invalidBackup());
        return labelqt::core::AtomicFileWriter::writeNew(target, bytes);
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
        const auto bytes = BoundedJsonIo::readBytes(entry.snapshotPath, BoundedJsonIo::SnapshotBytes, invalidBackup());
        if (!bytes || QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256) != entry.digest)
            continue;
        if (QFile::remove(entry.snapshotPath))
            QFile::remove(entry.recordPath);
    }
}

std::expected<FileRenameTransaction::RecoveryRecord, QString>
ProjectRecoveryService::recoveryDetails(const QString& recordPath)
{
    return FileRenameTransaction::readRecoveryRecord(recordPath);
}

std::expected<void, QString> ProjectRecoveryService::recoverFiles(const QString& recordPath, bool complete)
{
    try {
        const auto unsafe = []() -> std::expected<void, QString> {
            return std::unexpected(QCoreApplication::translate(
                "ProjectRecoveryService",
                "Recovery is ambiguous or files have changed. No automatic recovery is possible."));
        };
        const auto record = recoveryDetails(recordPath);
        if (!record || !record->canRecover)
            return unsafe();
        const QString& project = record->projectPath;
        const auto& before = record->before;
        const auto& after = record->after;
        if (QFileInfo(project).isSymLink() || !QFileInfo(project).isFile())
            return unsafe();
        ProjectFileTransaction transaction(project);
        const auto revision = FileRevision::read(project);
        if (!revision || !revision->exists ||
            (revision->sha256 != QCryptographicHash::hash(before, QCryptographicHash::Sha256) &&
             revision->sha256 != QCryptographicHash::hash(after, QCryptographicHash::Sha256)))
            return unsafe();

        struct RecoveryMove {
            QString current, temporary, destination;
            QByteArray digest;
        };
        QVector<RecoveryMove> moves;
        QSet<QString> allPaths, occupied, destinations, temporaryPaths;
        QSet<QByteArray> hashes;
        for (const auto& item : record->moves) {
            const QString& source = item.source;
            const QString& target = item.target;
            const QString& temporary = item.temporary;
            const QByteArray& hash = item.digest;
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
