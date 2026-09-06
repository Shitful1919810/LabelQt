#include "core/Project.h"

#include <algorithm>
#include <utility>

namespace labelqt::core {

bool Project::isEmpty() const noexcept
{
    return m_images.isEmpty();
}

void Project::clear()
{
    m_id = QUuid::createUuid();
    m_groupIds.clear();
    m_images.clear();
    m_groups.clear();
    m_commentLines.clear();
    m_filePath.clear();
    m_sourceName.clear();
}

const QVector<ImageEntry>& Project::images() const noexcept
{
    return m_images;
}

QVector<ImageEntry>& Project::images() noexcept
{
    return m_images;
}

int Project::imageIndexByName(const QString& imageName) const noexcept
{
    for (int index = 0; index < m_images.size(); ++index) {
        if (m_images.at(index).name == imageName) {
            return index;
        }
    }
    return -1;
}

const ImageEntry* Project::imageByName(const QString& imageName) const noexcept
{
    const int index = imageIndexByName(imageName);
    return index < 0 ? nullptr : &m_images.at(index);
}

ImageEntry* Project::imageByName(const QString& imageName) noexcept
{
    const int index = imageIndexByName(imageName);
    return index < 0 ? nullptr : &m_images[index];
}

int Project::activeLabelCountForGroup(const QString& group) const noexcept
{
    int count = 0;
    for (const ImageEntry& image : m_images) {
        count += static_cast<int>(std::ranges::count_if(image.labels, [&group](const Label& label) {
            return !label.isDeleted() && label.group() == group;
        }));
    }
    return count;
}

const QStringList& Project::groups() const noexcept
{
    return m_groups;
}

QString Project::groupName(const QUuid& id) const
{
    return id.isNull() ? QString() : m_groupIds.key(id);
}

void Project::setGroups(QStringList groups)
{
    QHash<QString, QUuid> ids;
    for (const auto& group : groups)
        ids.insert(group, m_groupIds.contains(group) ? m_groupIds.value(group) : QUuid::createUuid());
    restoreGroups(std::move(groups), std::move(ids));
}

void Project::restoreGroups(QStringList groups, QHash<QString, QUuid> ids)
{
    m_groups = std::move(groups);
    m_groupIds = std::move(ids);
}

QString Project::filePath() const
{
    return m_filePath;
}

void Project::setFilePath(QString filePath)
{
    m_filePath = std::move(filePath);
}

QString Project::sourceName() const
{
    return m_sourceName;
}

void Project::setSourceName(QString sourceName)
{
    m_sourceName = std::move(sourceName);
}

const QStringList& Project::commentLines() const noexcept
{
    return m_commentLines;
}

QStringList& Project::commentLines() noexcept
{
    return m_commentLines;
}

void Project::setCommentLines(QStringList commentLines)
{
    m_commentLines = std::move(commentLines);
}

} // namespace labelqt::core
