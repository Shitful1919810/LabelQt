#include "services/proofread/ReviewSessionService.h"
#include "core/AtomicFileWriter.h"
#include "core/ProjectMetadataService.h"
#include "services/io/BoundedJsonIo.h"
#include "services/label/ProjectTextSearchService.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <cmath>
#include <limits>

namespace labelqt::services {
namespace {
QString invalidSession()
{
    return QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session.");
}

bool consumeText(const QString& text, qint64& remaining)
{
    if (text.size() > remaining)
        return false;
    remaining -= text.toUtf8().size();
    return remaining >= 0;
}

bool validSnapshot(const ReviewLabelSnapshot& snapshot, qint64& remaining)
{
    return snapshot.labelIndex >= -1 && std::isfinite(snapshot.position.x()) && std::isfinite(snapshot.position.y()) &&
           consumeText(snapshot.imageName, remaining) && consumeText(snapshot.text, remaining) &&
           consumeText(snapshot.group, remaining);
}

bool validSession(const ReviewSession& session)
{
    if (session.annotations.size() > BoundedJsonIo::RecordItems ||
        session.pairs.size() > BoundedJsonIo::RecordItems - session.annotations.size())
        return false;
    qint64 remaining = BoundedJsonIo::SnapshotBytes;
    for (auto it = session.annotations.begin(); it != session.annotations.end(); ++it) {
        const auto status = static_cast<int>(it->status);
        if (status < 0 || status > 2 || !consumeText(it.key(), remaining) || !consumeText(it->note, remaining))
            return false;
    }
    for (const auto& pair : session.pairs)
        if (!validSnapshot(pair.baseline, remaining) || !validSnapshot(pair.current, remaining))
            return false;
    return true;
}

std::expected<QByteArray, QString> validatedBytes(const ReviewSession& session)
{
    if (!validSession(session))
        return std::unexpected(invalidSession());
    return BoundedJsonIo::encode(ReviewSessionService::encode(session), BoundedJsonIo::Review, invalidSession());
}

bool validSnapshotObject(const QJsonValue& value)
{
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    return object.value("image").isString() && object.value("text").isString() && object.value("group").isString() &&
           BoundedJsonIo::isInteger(object.value("label"), -1, std::numeric_limits<int>::max()) &&
           object.value("x").isDouble() && std::isfinite(object.value("x").toDouble()) &&
           object.value("y").isDouble() && std::isfinite(object.value("y").toDouble());
}

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
    QJsonObject object{{"page", change.imageName},
                   {"baselinePage", change.baselineImageName},
                   {"before", snapshotObject(change.baseline)},
                   {"after", snapshotObject(change.current)},
                   {"kind", static_cast<int>(change.kind)},
                   {"order", change.orderChanged},
                   {"manual", change.manuallyMatched}};
    if (!change.noteRevision.isEmpty()) object.insert("notes", QString::fromLatin1(change.noteRevision.toHex()));
    return digest(object);
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
std::expected<void, QString> ReviewSessionService::validate(const ReviewSession& session)
{
    const auto bytes = validatedBytes(session);
    if (!bytes)
        return std::unexpected(bytes.error());
    return {};
}
std::expected<ReviewSession, QString> ReviewSessionService::decode(const QJsonObject& object)
{
    ReviewSession session;
    if (object.isEmpty())
        return session;
    if (!BoundedJsonIo::encode(object, BoundedJsonIo::Review, invalidSession()) ||
        !BoundedJsonIo::isInteger(object.value("version"), 1, 1) || !object.value("annotations").isObject() ||
        !object.value("pairs").isArray() ||
        object.value("annotations").toObject().size() > BoundedJsonIo::RecordItems ||
        object.value("pairs").toArray().size() >
            BoundedJsonIo::RecordItems - object.value("annotations").toObject().size())
        return std::unexpected(
            QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session."));
    const auto annotations = object.value("annotations").toObject();
    for (auto it = annotations.begin(); it != annotations.end(); ++it) {
        const auto item = it.value().toObject();
        const int status = item.value("status").toInt(-1);
        if (!it.value().isObject() || !BoundedJsonIo::isInteger(item.value("status"), 0, 2) ||
            !item.value("note").isString())
            return std::unexpected(
                QCoreApplication::translate("ReviewSessionService", "Unsupported or damaged review session."));
        session.annotations.insert(it.key(), {static_cast<ReviewStatus>(status), item.value("note").toString()});
    }
    for (const auto& value : object.value("pairs").toArray()) {
        const auto pair = value.toObject();
        if (!value.isObject() || !validSnapshotObject(pair.value("before")) ||
            !validSnapshotObject(pair.value("after")))
            return std::unexpected(invalidSession());
        session.pairs.append(
            {snapshotFromObject(pair.value("before").toObject()), snapshotFromObject(pair.value("after").toObject())});
    }
    if (!validSession(session))
        return std::unexpected(invalidSession());
    return session;
}
std::expected<ReviewSession, QString> ReviewSessionService::loadLocal(const QString& key)
{
    const auto path = localPath(key);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return ReviewSession{};
    const auto object = BoundedJsonIo::read(path, BoundedJsonIo::Review, invalidSession());
    if (!object)
        return std::unexpected(object.error());
    return decode(*object);
}
std::expected<void, QString> ReviewSessionService::saveLocal(const QString& key, const ReviewSession& session)
{
    const auto path = localPath(key);
    if (!BoundedJsonIo::writableFile(path))
        return std::unexpected(invalidSession());
    const auto bytes = validatedBytes(session);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return std::unexpected(path);
    return labelqt::core::AtomicFileWriter::write(path, *bytes);
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
