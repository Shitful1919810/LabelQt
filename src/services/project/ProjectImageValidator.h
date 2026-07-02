#pragma once

#include "core/Project.h"

#include <QVector>

namespace labelqt::services {

struct MissingProjectImage {
    int imageIndex{-1};
    QString imageName;
    QString imagePath;
};

class ProjectImageValidator final {
public:
    static QVector<MissingProjectImage> missingImages(const labelqt::core::Project& project);
};

} // namespace labelqt::services
