#pragma once

#include "core/Label.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace labelqt::core {

class ImageEntry {
public:
    QString name;
    QString path;
    QVector<Label> labels;
};

class Project {
public:
    bool isEmpty() const noexcept;
    void clear();

    const QVector<ImageEntry>& images() const noexcept;
    QVector<ImageEntry>& images() noexcept;

    const QStringList& groups() const noexcept;
    QStringList& groups() noexcept;
    void setGroups(QStringList groups);

    QString filePath() const;
    void setFilePath(QString filePath);

    QString sourceName() const;
    void setSourceName(QString sourceName);

    const QStringList& commentLines() const noexcept;
    QStringList& commentLines() noexcept;
    void setCommentLines(QStringList commentLines);

private:
    QVector<ImageEntry> m_images;
    QStringList m_groups;
    QStringList m_commentLines;
    QString m_filePath;
    QString m_sourceName;
};

} // namespace labelqt::core
