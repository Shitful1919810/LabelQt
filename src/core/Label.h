#pragma once

#include <QPointF>
#include <QString>
#include <QUuid>

namespace labelqt::core {

class Label {
public:
    Label();
    Label(QString text, QString group, QPointF normalizedPosition);

    QUuid id() const noexcept
    {
        return m_id;
    }
    Label duplicate() const;

    const QString& text() const noexcept;
    void setText(QString text);

    const QString& group() const noexcept;
    void setGroup(QString group);

    QPointF position() const noexcept;
    void setPosition(QPointF normalizedPosition);

    bool isDeleted() const noexcept;
    void setDeleted(bool deleted) noexcept;

private:
    friend class ProjectIdentityService;
    QUuid m_id{QUuid::createUuid()};
    QString m_text;
    QString m_group;
    QPointF m_position{0.0, 0.0};
    bool m_deleted{false};
};

} // namespace labelqt::core
