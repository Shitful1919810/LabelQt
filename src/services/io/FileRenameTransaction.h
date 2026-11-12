#pragma once

#include "services/io/RenameDirectoryLocks.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <expected>
#include <functional>

class QJsonObject;

namespace labelqt::services {

class FileRenameTransaction final {
public:
    struct Move {
        QString source;
        QString target;
    };
    struct RecoveryMove {
        QString source, target, temporary;
        QByteArray digest;
        bool operator==(const RecoveryMove&) const = default;
    };
    struct RecoveryRecord {
        int version{0};
        QString projectPath;
        QByteArray before, after;
        QVector<RecoveryMove> moves;
        bool canRecover{false};
    };
    using RenameFile = std::function<bool(const QString&, const QString&)>;

    explicit FileRenameTransaction(QVector<Move> moves, QString recoveryDirectory = {}, RenameFile renameFile = {});
    ~FileRenameTransaction();
    FileRenameTransaction(const FileRenameTransaction&) = delete;
    FileRenameTransaction& operator=(const FileRenameTransaction&) = delete;

    std::expected<void, QString> apply();
    QStringList rollback();
    void commit();
    QString recoveryPath() const;
    static std::expected<void, QString> checkRecoveryDirectory(const QString& directory);
    static std::expected<RecoveryRecord, QString> readRecoveryRecord(const QString& path);
    static std::expected<RecoveryRecord, QString> decodeRecoveryRecord(const QJsonObject& object);
    void setProjectCommit(QString path, QByteArray before, QByteArray after);

private:
    enum class Location { Source, Temporary, Target };
    struct Step {
        Move move;
        QString temporary;
        Location location{Location::Source};
    };
    bool move(Step& step, Location destination);
    QString path(const Step& step, Location location) const;

    QVector<Step> m_steps;
    RenameFile m_renameFile;
    QString m_recoveryPath;
    QString m_projectPath;
    QByteArray m_projectBefore;
    QByteArray m_projectAfter;
    RenameDirectoryLocks m_directoryLocks;
    bool m_finished{false};
    bool m_recordWritten{false};
};

} // namespace labelqt::services
