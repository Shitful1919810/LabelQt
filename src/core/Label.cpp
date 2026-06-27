#include "core/Label.h"

#include <QUuid>

#include <algorithm>
#include <utility>

namespace labelqt::core {

namespace {
double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}
} // namespace

Label::Label()
{
    ensureStableId();
}

Label::Label(QString text, QString group, QPointF normalizedPosition)
    : m_text(std::move(text)), m_group(std::move(group))
{
    ensureStableId();
    setPosition(normalizedPosition);
}

const QString& Label::stableId() const noexcept
{
    return m_stableId;
}

void Label::setStableId(QString stableId)
{
    m_stableId = std::move(stableId);
}

void Label::resetStableId()
{
    m_stableId.clear();
    ensureStableId();
}

void Label::ensureStableId()
{
    if (m_stableId.isEmpty()) {
        m_stableId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
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
