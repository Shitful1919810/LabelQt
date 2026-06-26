#include "services/ProjectImageValidator.h"

#include <QFileInfo>

#include <ranges>

namespace labelqt::services {

QVector<MissingProjectImage> ProjectImageValidator::missingImages(const labelqt::core::Project& project)
{
    QVector<MissingProjectImage> missingImages;
    const QVector<labelqt::core::ImageEntry>& images = project.images();
    missingImages.reserve(images.size());

    for (int i : std::views::iota(0, static_cast<int>(images.size()))) {
        const labelqt::core::ImageEntry& image = images.at(i);
        const QFileInfo fileInfo(image.path);
        if (fileInfo.exists() && fileInfo.isFile()) {
            continue;
        }

        missingImages.append(MissingProjectImage{i, image.name, image.path});
    }
    return missingImages;
}

} // namespace labelqt::services
