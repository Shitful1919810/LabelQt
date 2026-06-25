#pragma once

#include "core/Project.h"

#include <QHash>
#include <QString>

namespace labelqt::services {

struct PageSourceInfo {
    int sourceIndex{-1};
    QString sourcePath;
};

class PageSourceInfoService final {
public:
    static QHash<QString, PageSourceInfo> sourcesForProject(const labelqt::core::Project& project);
    static void rewriteCommentLinesForCurrentImageOrder(labelqt::core::Project& project,
                                                        const QHash<QString, PageSourceInfo>& sourcesByImageName);
};

} // namespace labelqt::services
