#pragma once

#include "core/Project.h"

#include <QVector>

namespace labelqt::services {

class ProjectPageOrderService final {
public:
    static bool isValidOrder(const QVector<int>& order, int pageCount) noexcept;
    static bool isIdentityOrder(const QVector<int>& order) noexcept;
    static QVector<labelqt::core::ImageEntry> reorderedImages(const QVector<labelqt::core::ImageEntry>& images,
                                                                 const QVector<int>& order);
    static void reorderImages(labelqt::core::Project& project, const QVector<int>& order);
};

} // namespace labelqt::services
