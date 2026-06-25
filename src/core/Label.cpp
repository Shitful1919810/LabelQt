#include "core/Label.h"

#include <algorithm>
#include <utility>

namespace labelqt::core {

namespace {
double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}
} // namespace

Label::Label(QString text, QString group, QPointF normalizedPosition)
    : m_text(std::move(text)), m_group(std::move(group))
{
    setPosition(normalizedPosition);
}

const QString& Label::text() const noexcept
{
    return m_text;
}

void Label::setText(QString text)
{
    m_text = std::move(text);
}

const QString& Label::group() const noexcept
{
    return m_group;
}

void Label::setGroup(QString group)
{
    m_group = std::move(group);
}

QPointF Label::position() const noexcept
{
    return m_position;
}

void Label::setPosition(QPointF normalizedPosition)
{
    m_position = QPointF(clampUnit(normalizedPosition.x()), clampUnit(normalizedPosition.y()));
}

bool Label::isDeleted() const noexcept
{
    return m_deleted;
}

void Label::setDeleted(bool deleted) noexcept
{
    m_deleted = deleted;
}

} // namespace labelqt::core
