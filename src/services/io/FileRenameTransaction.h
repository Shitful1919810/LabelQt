#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <expected>
#include <functional>

namespace labelqt::services {

class FileRenameTransaction final {
public:
    struct Move {
        QString source;
        QString target;
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
    bool m_finished{false};
};

} // namespace labelqt::services
