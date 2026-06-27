#pragma once

#include <QPointF>
#include <QString>

namespace labelqt::core {

class Label {
public:
    Label();
    Label(QString text, QString group, QPointF normalizedPosition);

    const QString& text() const noexcept;
    void setText(QString text);

    const QString& group() const noexcept;
    void setGroup(QString group);

    QPointF position() const noexcept;
    void setPosition(QPointF normalizedPosition);

    bool isDeleted() const noexcept;
    void setDeleted(bool deleted) noexcept;

private:
    QString m_text;
    QString m_group;
    QPointF m_position{0.0, 0.0};
    bool m_deleted{false};
};

} // namespace labelqt::core
