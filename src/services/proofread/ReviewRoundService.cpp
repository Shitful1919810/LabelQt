#include "services/proofread/ReviewRoundService.h"

#include "core/ProjectIdentityService.h"
#include "core/ProjectMetadataService.h"
#include "services/proofread/ProjectComparisonService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace labelqt::services {
namespace {
using namespace labelqt::core;
QString errorText()
{
    return QCoreApplication::translate("ReviewRoundService",
                                       "The proofreading record is invalid or unsupported. It has not been changed.");
}
void require(bool condition)
{
    if (!condition)
        throw std::runtime_error(errorText().toStdString());
}

QJsonObject encodeProject(const Project& project, const QString& path)
{
    QJsonArray pages;
    const QDir directory(QFileInfo(path).absolutePath());
    for (const auto& page : project.images()) {
        QJsonArray labels;
        for (const auto& label : page.labels) {
            require(!label.isDeleted());
            labels.append(QJsonObject{{"text", label.text()},
                                      {"group", label.group()},
                                      {"x", label.position().x()},
                                      {"y", label.position().y()}});
        }
        pages.append(QJsonObject{{"name", page.name},
                                 {"path", page.path.isEmpty() ? QString() : directory.relativeFilePath(page.path)},
                                 {"labels", labels}});
    }
    return {{"groups", QJsonArray::fromStringList(project.groups())},
            {"pages", pages},
            {"identities", ProjectIdentityService::encode(project)}};
}

Project decodeProject(const QJsonObject& object, const QString& path)
{
    require(object.value("groups").isArray() && object.value("pages").isArray() &&
            object.value("identities").isObject());
    Project project;
    project.setFilePath(path);
    QStringList groups;
    for (const auto& value : object.value("groups").toArray()) {
        require(value.isString() && !value.toString().isEmpty());
        groups.append(value.toString());
    }
    project.setGroups(groups);
    const QDir directory(QFileInfo(path).absolutePath());
    for (const auto& value : object.value("pages").toArray()) {
        const auto pageObject = value.toObject();
        require(value.isObject() && pageObject.value("name").isString() && pageObject.value("path").isString() &&
                pageObject.value("labels").isArray());
        ImageEntry page;
        page.name = pageObject.value("name").toString();
        const auto imagePath = pageObject.value("path").toString();
        page.path = imagePath.isEmpty() ? QString() : QDir::cleanPath(directory.absoluteFilePath(imagePath));
        for (const auto& item : pageObject.value("labels").toArray()) {
            const auto label = item.toObject();
            require(item.isObject() && label.value("text").isString() && label.value("group").isString() &&
                    label.value("x").isDouble() && label.value("y").isDouble());
            const double x = label.value("x").toDouble(), y = label.value("y").toDouble();
            require(std::isfinite(x) && std::isfinite(y) && groups.contains(label.value("group").toString()));
            page.labels.append(Label(label.value("text").toString(), label.value("group").toString(), {x, y}));
        }
        project.images().append(std::move(page));
    }
    require(ProjectIdentityService::restore(project, object.value("identities").toObject()).has_value());
    // Snapshots must not silently regenerate identities as live documents may do after external editing.
    require(ProjectIdentityService::encode(project) == object.value("identities").toObject());
    return project;
}

ReviewLabelSnapshot labelSnapshot(const ImageEntry& page, int index)
{
    const auto& label = page.labels.at(index);
    return {page.name, label.text(), label.group(), label.position(), index, label.id()};
}

void refreshImagePaths(Project& snapshot, const Project& live)
{
    for (auto& page : snapshot.images()) {
        for (const auto& current : live.images()) {
            if (page.id == current.id) {
                page.path = current.path;
                break;
            }
        }
    }
}
} // namespace

labelqt::core::Project ReviewRoundService::snapshot(const labelqt::core::Project& project)
{
    auto result = project;
    result.setCommentLines({});
    for (auto& page : result.images())
        page.labels.removeIf([](const auto& label) { return label.isDeleted(); });
    return result;
}

std::expected<std::optional<ReviewRound>, QString> ReviewRoundService::read(const labelqt::core::Project& project)
{
    try {
        const auto root = ProjectMetadataService::tryMetadataObject(project.commentLines());
        if (!root)
            return std::unexpected(root.error());
        if (!root->contains("proofreading")) {
            if (root->contains("review")) {
                const auto legacyObject = root->value("review").toObject();
                require(legacyObject.value("version").toInt() == 1 &&
                        legacyObject.value("mode").toString() == QStringLiteral("baseline") &&
                        legacyObject.value("labels").isArray());
                QSet<QString> keys;
                for (const auto& value : legacyObject.value("labels").toArray()) {
                    const auto label = value.toObject();
                    const int index = label.value("labelIndex").toInt(-1);
                    require(index >= 0 && label.value("labelIndex").toDouble(-1) == index &&
                            !label.value("page").toString().isEmpty() && label.value("text").isString() &&
                            label.value("group").isString() && label.value("x").isDouble() &&
                            label.value("y").isDouble());
                    const auto key = ReviewMetadataService::keyForLabel(label.value("page").toString(), index);
                    require(!keys.contains(key));
                    keys.insert(key);
                }
            }
            const auto legacy = ReviewMetadataService::metadataForProject(project);
            if (!legacy.hasBaseline())
                return std::optional<ReviewRound>{};
            ReviewRound round;
            round.id = QUuid::createUuidV5(project.id(), QByteArrayLiteral("legacy-proofreading"));
            round.before.setFilePath(project.filePath());
            QStringList groups = project.groups();
            QHash<QString, QVector<ReviewLabelSnapshot>> labelsByPage;
            for (const auto& label : legacy.baselineLabels()) {
                require(label.labelIndex >= 0 && std::isfinite(label.position.x()) &&
                        std::isfinite(label.position.y()));
                labelsByPage[label.imageName].append(label);
                if (!groups.contains(label.group))
                    groups.append(label.group);
            }
            round.before.setGroups(groups);
            auto ordered = labelsByPage.keys();
            ordered.sort();
            for (const auto& name : ordered) {
                ImageEntry page;
                page.name = name;
                if (const auto* live = project.imageByName(name))
                    page.path = live->path;
                auto& labels = labelsByPage[name];
                std::ranges::sort(labels, {}, &ReviewLabelSnapshot::labelIndex);
                for (const auto& label : labels)
                    page.labels.append(Label(label.text, label.group, label.position));
                round.before.images().append(std::move(page));
            }
            round.session = ReviewSessionService::projectSession(project);
            require(ReviewSessionService::decode(round.session).has_value());
            return std::optional<ReviewRound>{std::move(round)};
        }
        const auto object = root->value("proofreading").toObject();
        require(object.value("version").toInt() == 1 && object.value("notes").isArray() &&
                object.value("session").isObject());
        ReviewRound round;
        round.id = QUuid(object.value("id").toString());
        require(!round.id.isNull());
        round.before = decodeProject(object.value("before").toObject(), project.filePath());
        if (object.contains("after"))
            round.after = decodeProject(object.value("after").toObject(), project.filePath());
        const auto notes = object.value("notes").toArray();
        require(notes.size() <= 10000);
        QSet<QUuid> ids;
        for (const auto& value : notes) {
            const auto item = value.toObject();
            ProofreadNote note;
            note.id = QUuid(item.value("id").toString());
            note.pageId = QUuid(item.value("pageId").toString());
            note.labelId = QUuid(item.value("labelId").toString());
            require(!note.id.isNull() && !note.pageId.isNull() && !ids.contains(note.id) &&
                    item.value("pageName").isString() && item.value("quote").isString() &&
                    item.value("text").isString() && item.value("resolved").isBool() &&
                    (item.value("labelId").toString().isEmpty() || !note.labelId.isNull()));
            ids.insert(note.id);
            note.pageName = item.value("pageName").toString();
            note.quote = item.value("quote").toString();
            note.text = item.value("text").toString();
            note.resolved = item.value("resolved").toBool();
            if (item.contains("associatedPageId")) {
                note.associatedPageId = QUuid(item.value("associatedPageId").toString());
                note.associatedLabelId = QUuid(item.value("associatedLabelId").toString());
                require(!note.associatedPageId.isNull() && !note.associatedLabelId.isNull());
            }
            require(note.text.size() <= 65536 && note.quote.size() <= 1048576);
            round.notes.append(std::move(note));
        }
        round.session = object.value("session").toObject();
        require(ReviewSessionService::decode(round.session).has_value());
        return std::optional<ReviewRound>{std::move(round)};
    }
    catch (const std::exception& error) {
        return std::unexpected(QString::fromUtf8(error.what()));
    }
}

ReviewRound ReviewRoundService::complete(ReviewRound round, const labelqt::core::Project& project)
{
    round.after = snapshot(project);
    for (auto& note : round.notes) {
        if (note.associatedPageId.isNull())
            continue;
        note.pageId = note.associatedPageId;
        note.labelId = note.associatedLabelId;
        const auto target = locateNote(project, note);
        if (target.first >= 0)
            note.pageName = project.images().at(target.first).name;
        note.associatedPageId = {};
        note.associatedLabelId = {};
    }
    return round;
}

QJsonObject ReviewRoundService::encode(const ReviewRound& round, const QString& projectPath)
{
    QJsonArray notes;
    for (const auto& note : round.notes) {
        QJsonObject item{{"id", note.id.toString()},
                         {"pageId", note.pageId.toString()},
                         {"labelId", note.labelId.isNull() ? QString() : note.labelId.toString()},
                         {"pageName", note.pageName},
                         {"quote", note.quote},
                         {"text", note.text},
                         {"resolved", note.resolved}};
        if (!note.associatedPageId.isNull()) {
            item.insert("associatedPageId", note.associatedPageId.toString());
            item.insert("associatedLabelId", note.associatedLabelId.toString());
        }
        notes.append(item);
    }
    QJsonObject object{{"version", 1},
                       {"id", round.id.toString()},
                       {"before", encodeProject(round.before, projectPath)},
                       {"notes", notes},
                       {"session", round.session}};
    if (round.after)
        object.insert("after", encodeProject(*round.after, projectPath));
    return object;
}

QPair<int, int> ReviewRoundService::locate(const labelqt::core::Project& project, const QUuid& pageId,
                                           const QUuid& labelId)
{
    for (int i = 0; i < project.images().size(); ++i) {
        const auto& page = project.images().at(i);
        if (page.id != pageId)
            continue;
        if (labelId.isNull())
            return {i, -1};
        for (int j = 0; j < page.labels.size(); ++j)
            if (page.labels.at(j).id() == labelId && !page.labels.at(j).isDeleted())
                return {i, j};
        return {i, -1};
    }
    return {-1, -1};
}

QString ReviewRoundService::noteText(const ProofreadNote& note)
{
    return QCoreApplication::translate("ReviewRoundService", "%1: %2")
        .arg(note.resolved ? QCoreApplication::translate("ReviewRoundService", "Resolved")
                           : QCoreApplication::translate("ReviewRoundService", "Open"),
             note.text);
}

QPair<int, int> ReviewRoundService::locateNote(const labelqt::core::Project& project, const ProofreadNote& note)
{
    return note.associatedPageId.isNull() ? locate(project, note.pageId, note.labelId)
                                          : locate(project, note.associatedPageId, note.associatedLabelId);
}

ReviewRoundPresentation ReviewRoundService::present(const ReviewRound& round, const labelqt::core::Project& live)
{
    ReviewRoundPresentation result{round.before, round.after ? *round.after : snapshot(live), {}, false};
    refreshImagePaths(result.before, live);
    refreshImagePaths(result.after, live);
    const auto plan = ProjectComparisonService::planComparison(result.before, result.after);
    result.changes = ProjectComparisonService::changesBetweenProjects(result.after, plan);
    for (auto& change : result.changes) {
        const auto* page = result.after.imageByName(change.imageName);
        if (!page)
            page = result.before.imageByName(change.baselineImageName);
        if (page)
            change.pageId = page->id;
    }
    for (const auto& before : result.before.images())
        for (const auto& after : result.after.images())
            if (before.id == after.id && before.labelIdentityGeneration != after.labelIdentityGeneration)
                result.uncertainIdentity = true;
    for (const auto& note : round.notes) {
        const bool useAssociation = !round.after && !note.associatedPageId.isNull();
        const QUuid pageId = useAssociation ? note.associatedPageId : note.pageId;
        const QUuid labelId = useAssociation ? note.associatedLabelId : note.labelId;
        QString renderedNote = noteText(note);
        const auto target = locate(result.after, pageId, labelId);
        if (target.first < 0 || (!labelId.isNull() && target.second < 0))
            renderedNote.prepend(
                QCoreApplication::translate("ReviewRoundService",
                                            "Original label unavailable; association needs confirmation.") +
                QLatin1Char('\n'));
        const auto appendNote = [&](ReviewChange& change) {
            if (!change.noteText.isEmpty())
                change.noteText += QLatin1Char('\n');
            change.noteText += renderedNote;
            change.noteRevision = QCryptographicHash::hash(
                change.noteRevision + note.id.toRfc4122() + note.text.toUtf8(), QCryptographicHash::Sha256);
        };
        bool attached = false;
        for (auto& change : result.changes) {
            const bool sameLabel = !labelId.isNull() && (change.baseline.id == labelId || change.current.id == labelId);
            const bool samePageNote = labelId.isNull() && target.first >= 0 &&
                                      change.kind == ReviewChangeKind::Comment && change.current.id.isNull() &&
                                      change.imageIndex == target.first;
            if (sameLabel || samePageNote) {
                appendNote(change);
                attached = true;
                break;
            }
        }
        if (attached)
            continue;
        ReviewChange change;
        change.kind = ReviewChangeKind::Comment;
        change.pageId = pageId;
        appendNote(change);
        const auto [pageIndex, labelIndex] = target;
        change.imageIndex = pageIndex;
        change.imageName = pageIndex >= 0 ? result.after.images().at(pageIndex).name : note.pageName;
        change.baselineImageName = change.imageName;
        change.labelIndex = change.currentLabelIndex = labelIndex;
        if (labelIndex >= 0)
            change.current = labelSnapshot(result.after.images().at(pageIndex), labelIndex);
        else {
            change.current.text = note.quote;
            change.current.id = labelId;
        }
        change.baseline = change.current;
        result.changes.append(std::move(change));
    }
    for (auto& change : result.changes)
        change.jumpAvailable = locate(live, change.pageId, {}).first >= 0;
    return result;
}

QJsonObject ReviewRoundService::rebase(const QJsonObject& section, const QString& fromPath, const QString& toPath)
{
    if (QFileInfo(fromPath).absolutePath() == QFileInfo(toPath).absolutePath())
        return section;
    auto result = section;
    for (const QString& key : {QStringLiteral("before"), QStringLiteral("after")}) {
        if (!result.contains(key))
            continue;
        result.insert(key, encodeProject(decodeProject(result.value(key).toObject(), fromPath), toPath));
    }
    return result;
}
} // namespace labelqt::services
