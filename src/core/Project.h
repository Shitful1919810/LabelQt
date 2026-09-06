#pragma once

#include "core/Label.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace labelqt::core {

class ImageEntry {
public:
    QString name;
    QString path;
    QVector<Label> labels;
    QUuid id{QUuid::createUuid()};
};

class Project {
public:
    QUuid id() const noexcept
    {
        return m_id;
    }
    QUuid groupId(const QString& name) const
    {
        return m_groupIds.value(name);
    }
    QString groupName(const QUuid& id) const;
    const QHash<QString, QUuid>& groupIds() const noexcept
    {
        return m_groupIds;
    }
    void restoreGroups(QStringList groups, QHash<QString, QUuid> ids);
    bool isEmpty() const noexcept;
    void clear();

    const QVector<ImageEntry>& images() const noexcept;
    QVector<ImageEntry>& images() noexcept;
    int imageIndexByName(const QString& imageName) const noexcept;
    const ImageEntry* imageByName(const QString& imageName) const noexcept;
    ImageEntry* imageByName(const QString& imageName) noexcept;
    int activeLabelCountForGroup(const QString& group) const noexcept;

    const QStringList& groups() const noexcept;
    void setGroups(QStringList groups);

    QString filePath() const;
    void setFilePath(QString filePath);

    QString sourceName() const;
    void setSourceName(QString sourceName);

    const QStringList& commentLines() const noexcept;
    QStringList& commentLines() noexcept;
    void setCommentLines(QStringList commentLines);

private:
    friend class ProjectIdentityService;
    QUuid m_id{QUuid::createUuid()};
    QHash<QString, QUuid> m_groupIds;
    QVector<ImageEntry> m_images;
    QStringList m_groups;
    QStringList m_commentLines;
    QString m_filePath;
    QString m_sourceName;
};

} // namespace labelqt::core
