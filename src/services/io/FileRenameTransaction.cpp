#include "services/io/FileRenameTransaction.h"

#include "core/AtomicFileWriter.h"
#include "core/LabelPlusDocument.h"
#include "services/io/BoundedJsonIo.h"
#include "services/io/FileRevision.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <utility>

namespace labelqt::services {
namespace {
QString invalidRecord()
{
    return QCoreApplication::translate(
        "ProjectRecoveryService", "Recovery is ambiguous or files have changed. No automatic recovery is possible.");
}

bool absolutePath(const QString& path)
{
    return !path.isEmpty() && !path.contains(QChar::Null) && QFileInfo(path).isAbsolute() &&
           QDir::cleanPath(path) == path;
}
} // namespace

std::expected<FileRenameTransaction::RecoveryRecord, QString>
FileRenameTransaction::decodeRecoveryRecord(const QJsonObject& object)
{
    if (!BoundedJsonIo::encode(object, BoundedJsonIo::Rename, invalidRecord()) ||
        !BoundedJsonIo::isInteger(object.value("version"), 1, 2) || !object.value("moves").isArray())
        return std::unexpected(invalidRecord());
    RecoveryRecord record;
    record.version = object.value("version").toInt();
    record.projectPath = object.value("project").toString();
    const auto moves = object.value("moves").toArray();
    if (moves.isEmpty() || moves.size() > BoundedJsonIo::RecordItems)
        return std::unexpected(invalidRecord());
    QSet<QString> sources, targets, temporaries;
    QSet<QByteArray> digests;
    for (const auto& value : moves) {
        if (!value.isObject())
            return std::unexpected(invalidRecord());
        const auto item = value.toObject();
        RecoveryMove move{
            item.value("source").toString(), item.value("target").toString(), item.value("temporary").toString(), {}};
        if (move.source.isEmpty() || move.target.isEmpty())
            return std::unexpected(invalidRecord());
        if (record.version == 2) {
            if (!absolutePath(move.source) || !absolutePath(move.target) || !absolutePath(move.temporary) ||
                !BoundedJsonIo::isDigest(item.value("sha256")) || sources.contains(move.source) ||
                targets.contains(move.target) || temporaries.contains(move.temporary))
                return std::unexpected(invalidRecord());
            move.digest = QByteArray::fromHex(item.value("sha256").toString().toLatin1());
            sources.insert(move.source);
            targets.insert(move.target);
            temporaries.insert(move.temporary);
            digests.insert(move.digest);
        }
        record.moves.append(std::move(move));
    }
    if (record.version == 1)
        return record; // Legacy mappings remain available for manual inspection.
    for (const auto& temporary : temporaries)
        if (sources.contains(temporary) || targets.contains(temporary))
            return std::unexpected(invalidRecord());
    if (!object.value("project").isString() || !object.value("before").isString() || !object.value("after").isString())
        return std::unexpected(invalidRecord());
    const auto before = object.value("before").toString();
    const auto after = object.value("after").toString();
    if (record.projectPath.isEmpty() && before.isEmpty() && after.isEmpty())
        return record; // File-only transactions predate project snapshot recovery.
    constexpr auto encodedMaximum = ((BoundedJsonIo::SnapshotBytes + 2) / 3) * 4;
    if (!absolutePath(record.projectPath) || before.isEmpty() || after.isEmpty() || before.size() > encodedMaximum ||
        after.size() > encodedMaximum || digests.size() != moves.size() || sources.contains(record.projectPath) ||
        targets.contains(record.projectPath) || temporaries.contains(record.projectPath))
        return std::unexpected(invalidRecord());
    const auto decodeSnapshot = [](const QString& text) -> std::expected<QByteArray, QString> {
        const auto encoded = text.toLatin1();
        if (QString::fromLatin1(encoded) != text)
            return std::unexpected(invalidRecord());
        auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > BoundedJsonIo::SnapshotBytes)
            return std::unexpected(invalidRecord());
        return std::move(decoded.decoded);
    };
    auto decodedBefore = decodeSnapshot(before), decodedAfter = decodeSnapshot(after);
    if (!decodedBefore || !decodedAfter)
        return std::unexpected(invalidRecord());
    record.before = std::move(*decodedBefore);
    record.after = std::move(*decodedAfter);
    try {
        labelqt::core::LabelPlusDocument::parse(QString::fromUtf8(record.before), record.projectPath);
        labelqt::core::LabelPlusDocument::parse(QString::fromUtf8(record.after), record.projectPath);
    }
    catch (const std::exception&) {
        return std::unexpected(invalidRecord());
    }
    record.canRecover = true;
    return record;
}

std::expected<FileRenameTransaction::RecoveryRecord, QString>
FileRenameTransaction::readRecoveryRecord(const QString& path)
{
    const auto object = BoundedJsonIo::read(path, BoundedJsonIo::Rename, invalidRecord());
    if (!object)
        return std::unexpected(object.error());
    auto record = decodeRecoveryRecord(*object);
    if (record && record->canRecover && QFileInfo(record->projectPath).absolutePath() != QFileInfo(path).absolutePath())
        return std::unexpected(invalidRecord());
    return record;
}

FileRenameTransaction::FileRenameTransaction(QVector<Move> moves, QString recoveryDirectory, RenameFile renameFile)
    : m_renameFile(renameFile ? std::move(renameFile) : RenameFile([](const QString& from, const QString& to) {
          return QFile::rename(from, to);
      }))
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (Move& item : moves) {
        const QString temporary =
            QFileInfo(item.source)
                .dir()
                .filePath(QStringLiteral(".labelqt-rename-%1-%2.tmp").arg(id).arg(m_steps.size()));
        m_steps.append({std::move(item), temporary});
    }
    if (!recoveryDirectory.isEmpty() && !m_steps.isEmpty()) {
        m_recoveryPath = QDir(recoveryDirectory).filePath(QStringLiteral(".labelqt-rename-%1.json").arg(id));
    }
}

FileRenameTransaction::~FileRenameTransaction()
{
    if (!m_finished)
        rollback();
}

QString FileRenameTransaction::path(const Step& step, Location location) const
{
    switch (location) {
    case Location::Source:
        return step.move.source;
    case Location::Temporary:
        return step.temporary;
    case Location::Target:
        return step.move.target;
    }
    Q_UNREACHABLE();
}

bool FileRenameTransaction::move(Step& step, Location destination)
{
    if (!m_renameFile(path(step, step.location), path(step, destination)))
        return false;
    step.location = destination;
    return true;
}

std::expected<void, QString> FileRenameTransaction::apply()
{
    if (m_finished || m_recordWritten || m_steps.size() > BoundedJsonIo::RecordItems ||
        m_projectBefore.size() > BoundedJsonIo::SnapshotBytes || m_projectAfter.size() > BoundedJsonIo::SnapshotBytes)
        return std::unexpected(invalidRecord());
    if (!m_recoveryPath.isEmpty()) {
        const auto recovered = checkRecoveryDirectory(QFileInfo(m_recoveryPath).absolutePath());
        if (!recovered)
            return recovered;
        QJsonArray moves;
        for (const Step& step : std::as_const(m_steps)) {
            if (QFileInfo(step.move.source).isSymLink() || QFileInfo(step.move.target).isSymLink() ||
                !BoundedJsonIo::writableFile(step.temporary, true))
                return std::unexpected(invalidRecord());
            const auto revision = FileRevision::read(step.move.source);
            if (!revision || !revision->exists)
                return std::unexpected(
                    QCoreApplication::translate("FileRenameTransaction", "A source file cannot be verified."));
            moves.append(QJsonObject{{QStringLiteral("source"), step.move.source},
                                     {QStringLiteral("temporary"), step.temporary},
                                     {QStringLiteral("target"), step.move.target},
                                     {QStringLiteral("sha256"), QString::fromLatin1(revision->sha256.toHex())}});
        }
        const QJsonObject object{{QStringLiteral("version"), 2},
                                 {QStringLiteral("moves"), moves},
                                 {QStringLiteral("project"), m_projectPath},
                                 {QStringLiteral("before"), QString::fromLatin1(m_projectBefore.toBase64())},
                                 {QStringLiteral("after"), QString::fromLatin1(m_projectAfter.toBase64())}};
        const auto record = decodeRecoveryRecord(object);
        const auto bytes = BoundedJsonIo::encode(object, BoundedJsonIo::Rename, invalidRecord());
        if (!record || !bytes || !BoundedJsonIo::writableFile(m_recoveryPath, true) ||
            (!m_projectPath.isEmpty() &&
             (QFileInfo(m_projectPath).isSymLink() || !QFileInfo(m_projectPath).isFile() ||
              QFileInfo(m_projectPath).absolutePath() != QFileInfo(m_recoveryPath).absolutePath())))
            return std::unexpected(invalidRecord());
        const auto written = labelqt::core::AtomicFileWriter::writeNew(m_recoveryPath, *bytes);
        if (!written)
            return written;
        m_recordWritten = true;
    }

    for (Location destination : {Location::Temporary, Location::Target}) {
        for (Step& step : m_steps) {
            const QString from = path(step, step.location);
            if (!move(step, destination)) {
                return std::unexpected(
                    QCoreApplication::translate("FileRenameTransaction", "Failed to rename %1 to %2.")
                        .arg(from, path(step, destination)));
            }
        }
    }
    return {};
}

QStringList FileRenameTransaction::rollback()
{
    if (m_finished)
        return {};
    QStringList failures;
    // Vacate every completed target before restoring any source; names can form chains or cycles.
    for (Step& step : m_steps) {
        if (step.location == Location::Target && !move(step, Location::Temporary)) {
            failures.append(QCoreApplication::translate("FileRenameTransaction", "Failed to rename %1 to %2.")
                                .arg(step.move.target, step.temporary));
        }
    }
    for (Step& step : m_steps) {
        if (step.location == Location::Temporary && !move(step, Location::Source)) {
            failures.append(QCoreApplication::translate("FileRenameTransaction", "Failed to rename %1 to %2.")
                                .arg(step.temporary, step.move.source));
        }
    }
    m_finished = true;
    if (failures.isEmpty()) {
        if (m_recordWritten)
            QFile::remove(m_recoveryPath);
    }
    else if (!m_recoveryPath.isEmpty()) {
        failures.append(QCoreApplication::translate(
                            "FileRenameTransaction",
                            "Recovery mapping retained at %1. Do not discard the remaining temporary files.")
                            .arg(m_recoveryPath));
    }
    return failures;
}

void FileRenameTransaction::commit()
{
    m_finished = true;
    if (m_recordWritten)
        QFile::remove(m_recoveryPath);
}

QString FileRenameTransaction::recoveryPath() const
{
    return m_recoveryPath;
}

void FileRenameTransaction::setProjectCommit(QString path, QByteArray before, QByteArray after)
{
    m_projectPath = std::move(path);
    m_projectBefore = std::move(before);
    m_projectAfter = std::move(after);
}

std::expected<void, QString> FileRenameTransaction::checkRecoveryDirectory(const QString& directory)
{
    const QDir dir(directory);
    const QStringList records = dir.entryList({QStringLiteral(".labelqt-rename-*.json")},
                                              QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (!records.isEmpty()) {
        return std::unexpected(
            QCoreApplication::translate("FileRenameTransaction",
                                        "Unresolved rename record: %1. Recover the files before continuing.")
                .arg(dir.absoluteFilePath(records.first())));
    }
    return {};
}

} // namespace labelqt::services
