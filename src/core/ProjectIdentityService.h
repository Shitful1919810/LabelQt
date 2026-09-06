#pragma once

#include "core/Project.h"
#include <QJsonObject>
#include <expected>

namespace labelqt::core {

class ProjectIdentityService final {
public:
    static bool valid(const Project& project);
    static QJsonObject encode(const Project& project);
    static std::expected<void, QString> restore(Project& project, const QJsonObject& section);
    static void regenerate(Project& project);
};

} // namespace labelqt::core
