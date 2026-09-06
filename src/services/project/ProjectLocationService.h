#pragma once

#include "core/Project.h"

namespace labelqt::services {

class ProjectLocationService final {
public:
    static QStringList rebaseComments(const QStringList& comments, const QString& fromPath, const QString& toPath);
    static labelqt::core::Project forOutput(const labelqt::core::Project& project, const QString& path);
};

} // namespace labelqt::services
