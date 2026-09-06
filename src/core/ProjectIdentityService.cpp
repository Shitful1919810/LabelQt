#include "core/ProjectIdentityService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QSet>
#include <utility>

namespace labelqt::core {
namespace {
QString uuid(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces);
}
QString name(const ImageEntry& page)
{
    return page.name.isEmpty() ? QFileInfo(page.path).fileName() : page.name;
}

QString signature(const ImageEntry& page)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    for (const auto& label : page.labels) {
        if (!label.isDeleted())
            stream << (label.text().isNull() ? QStringLiteral("") : label.text()) << label.group()
                   << QString::number(label.position().x(), 'f', 3) << QString::number(label.position().y(), 'f', 3);
    }
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
} // namespace

bool ProjectIdentityService::valid(const Project& project)
{
    QSet<QUuid> ids;
    const auto add = [&ids](const QUuid& id) {
        if (id.isNull() || ids.contains(id))
            return false;
        ids.insert(id);
        return true;
    };
    if (!add(project.id()) || project.groups().size() != project.groupIds().size())
        return false;
    for (const auto& group : project.groups())
        if (!add(project.groupId(group)))
            return false;
    for (const auto& page : project.images()) {
        if (!add(page.id))
            return false;
        for (const auto& label : page.labels)
            if (!add(label.id()))
                return false;
    }
    return true;
}

QJsonObject ProjectIdentityService::encode(const Project& project)
{
    QJsonObject groups;
    for (const auto& group : project.groups())
        groups.insert(group, uuid(project.groupId(group)));
    QJsonObject pages;
    for (const auto& page : project.images()) {
        QJsonArray labels;
        for (const auto& label : page.labels)
            if (!label.isDeleted())
                labels.append(uuid(label.id()));
        pages.insert(name(page),
                     QJsonObject{{"id", uuid(page.id)}, {"labels", labels}, {"signature", signature(page)}});
    }
    return {{"version", 1}, {"project", uuid(project.id())}, {"groups", groups}, {"pages", pages}};
}

std::expected<void, QString> ProjectIdentityService::restore(Project& project, const QJsonObject& section)
{
    if (section.isEmpty())
        return {};
    const auto invalid = [] {
        return std::unexpected(
            QCoreApplication::translate("LabelPlusDocument", "Invalid LabelPlus document structure."));
    };
    if (section.value("version").toInt() != 1 || !section.value("groups").isObject() ||
        !section.value("pages").isObject())
        return invalid();
    Project candidate = project;
    candidate.m_id = QUuid(section.value("project").toString());
    const auto groups = section.value("groups").toObject();
    for (const auto& group : candidate.groups())
        if (groups.contains(group))
            candidate.m_groupIds[group] = QUuid(groups.value(group).toString());
    const auto pages = section.value("pages").toObject();
    for (auto& page : candidate.images()) {
        if (!pages.contains(name(page)))
            continue;
        const auto entry = pages.value(name(page)).toObject();
        page.id = QUuid(entry.value("id").toString());
        // Foreign editors can alter/reorder labels without updating metadata. Never attach stale row identities.
        if (entry.value("signature").toString() != signature(page))
            continue;
        const auto labels = entry.value("labels").toArray();
        if (labels.size() != page.labels.size())
            return invalid();
        for (qsizetype i = 0; i < labels.size(); ++i)
            page.labels[i].m_id = QUuid(labels.at(i).toString());
    }
    if (!valid(candidate))
        return invalid();
    project = std::move(candidate);
    return {};
}

void ProjectIdentityService::regenerate(Project& project)
{
    project.m_id = QUuid::createUuid();
    for (auto& id : project.m_groupIds)
        id = QUuid::createUuid();
    for (auto& page : project.images()) {
        page.id = QUuid::createUuid();
        for (auto& label : page.labels)
            label = label.duplicate();
    }
}
} // namespace labelqt::core
