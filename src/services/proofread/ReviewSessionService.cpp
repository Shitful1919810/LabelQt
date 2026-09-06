#include "services/proofread/ReviewSessionService.h"
#include "core/AtomicFileWriter.h"
#include "core/ProjectMetadataService.h"
#include "services/label/ProjectTextSearchService.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace labelqt::services {
namespace {
QJsonObject snapshotObject(const ReviewLabelSnapshot& snapshot)
{
    return {{"image", snapshot.imageName}, {"label", snapshot.labelIndex}, {"text", snapshot.text},
            {"group", snapshot.group},     {"x", snapshot.position.x()},   {"y", snapshot.position.y()}};
}
ReviewLabelSnapshot snapshotFromObject(const QJsonObject& object)
{
    return {object.value("image").toString(),
            object.value("text").toString(),
            object.value("group").toString(),
            {object.value("x").toDouble(), object.value("y").toDouble()},
            object.value("label").toInt(-1)};
}
QString digest(const QJsonObject& object)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}
QString localPath(const QString& key)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(
            QStringLiteral("review-sessions/%1.json")
                .arg(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex())));
}
} // namespace
QString ReviewSessionService::snapshotFingerprint(const ReviewLabelSnapshot& snapshot)
{
    return digest(snapshotObject(snapshot));
}
QString ReviewSessionService::fingerprint(const ReviewChange& change)
{
    return digest({{"page", change.imageName},
                   {"baselinePage", change.baselineImageName},
                   {"before", snapshotObject(change.baseline)},
                   {"after", snapshotObject(change.current)},
                   {"kind", static_cast<int>(change.kind)},
                   {"order", change.orderChanged},
                   {"manual", change.manuallyMatched}});
}
QString ReviewSessionService::contextKey(const labelqt::core::Project& before, const labelqt::core::Project& current)
{
    return digest({{"before", QFileInfo(before.filePath()).absoluteFilePath()},
                   {"baselineRevision", QString::fromLatin1(ProjectTextSearchService::revision(before).toHex())},
                   {"current", QFileInfo(current.filePath()).absoluteFilePath()}});
}
QJsonObject ReviewSessionService::encode(const ReviewSession& session)
{
    QJsonObject annotations;
    for (auto it = session.annotations.begin(); it != session.annotations.end(); ++it)
        annotations.insert(it.key(), QJsonObject{{"status", static_cast<int>(it->status)}, {"note", it->note}});
    QJsonArray pairs;
    for (const auto& pair : session.pairs)
        pairs.append(QJsonObject{{"before", snapshotObject(pair.baseline)}, {"after", snapshotObject(pair.current)}});
    return {{"version", 1}, {"annotations", annotations}, {"pairs", pairs}};
}
std::expected<ReviewSession, QString> ReviewSessionService::decode(const QJsonObject& object)
{
    ReviewSession session;
    if (object.isEmpty())
        return session;
    if (object.value("version").toInt() != 1 || !object.value("annotations").isObject() ||
        !object.value("pairs").isArray())
        return std::unexpected(
            QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session."));
    const auto annotations = object.value("annotations").toObject();
    for (auto it = annotations.begin(); it != annotations.end(); ++it) {
        const auto item = it.value().toObject();
        const int status = item.value("status").toInt(-1);
        if (status < 0 || status > 2 || !item.value("note").isString())
            return std::unexpected(
                QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session."));
        session.annotations.insert(it.key(), {static_cast<ReviewStatus>(status), item.value("note").toString()});
    }
    for (const auto& value : object.value("pairs").toArray()) {
        const auto pair = value.toObject();
        session.pairs.append(
            {snapshotFromObject(pair.value("before").toObject()), snapshotFromObject(pair.value("after").toObject())});
    }
    return session;
}
std::expected<ReviewSession, QString> ReviewSessionService::loadLocal(const QString& key)
{
    QFile file(localPath(key));
    if (!file.exists())
        return ReviewSession{};
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(file.errorString());
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return std::unexpected(
            QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session."));
    return decode(document.object());
}
std::expected<void, QString> ReviewSessionService::saveLocal(const QString& key, const ReviewSession& session)
{
    const auto path = localPath(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return std::unexpected(path);
    return labelqt::core::AtomicFileWriter::write(path, QJsonDocument(encode(session)).toJson());
}
QJsonObject ReviewSessionService::projectSession(const labelqt::core::Project& project)
{
    return labelqt::core::ProjectMetadataService::metadataObject(project.commentLines())
        .value("reviewSession")
        .toObject();
}
QStringList ReviewSessionService::withProjectSession(const QStringList& comments, const QJsonObject& session)
{
    auto metadata = labelqt::core::ProjectMetadataService::metadataObject(comments);
    if (session.isEmpty())
        metadata.remove("reviewSession");
    else
        metadata.insert("reviewSession", session);
    return labelqt::core::ProjectMetadataService::rewriteCommentLines(comments, metadata);
}
} // namespace labelqt::services
