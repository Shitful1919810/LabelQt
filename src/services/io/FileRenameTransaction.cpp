#include "services/io/FileRenameTransaction.h"

#include "core/AtomicFileWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <utility>

namespace labelqt::services {

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
    if (!m_recoveryPath.isEmpty()) {
        const auto recovered = checkRecoveryDirectory(QFileInfo(m_recoveryPath).absolutePath());
        if (!recovered)
            return recovered;
        QJsonArray moves;
        for (const Step& step : std::as_const(m_steps)) {
            moves.append(QJsonObject{{QStringLiteral("source"), step.move.source},
                                     {QStringLiteral("temporary"), step.temporary},
                                     {QStringLiteral("target"), step.move.target}});
        }
        const auto written = labelqt::core::AtomicFileWriter::write(
            m_recoveryPath,
            QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("moves"), moves}}).toJson());
        if (!written)
            return written;
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
        if (!m_recoveryPath.isEmpty())
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
    if (!m_recoveryPath.isEmpty())
        QFile::remove(m_recoveryPath);
}

QString FileRenameTransaction::recoveryPath() const
{
    return m_recoveryPath;
}

std::expected<void, QString> FileRenameTransaction::checkRecoveryDirectory(const QString& directory)
{
    const QDir dir(directory);
    const QStringList records = dir.entryList({QStringLiteral(".labelqt-rename-*.json")}, QDir::Files | QDir::Hidden);
    if (!records.isEmpty()) {
        return std::unexpected(
            QCoreApplication::translate("FileRenameTransaction",
                                        "Unresolved rename record: %1. Recover the files before continuing.")
                .arg(dir.absoluteFilePath(records.first())));
    }
    return {};
}

} // namespace labelqt::services
