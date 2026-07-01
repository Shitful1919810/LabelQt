#pragma once

#include "services/ReviewMetadataService.h"

#include <QString>
#include <QVector>

namespace labelqt::services {

enum class ReviewChangeFacet {
    Text,
    Format,
    Group,
    Marker,
    Order,
};

class ReviewChangeClassifier final {
public:
    static QVector<ReviewChangeFacet> facets(const ReviewChange& change);
    static QString facetKey(ReviewChangeFacet facet);
    static QString kindKey(ReviewChangeKind kind);
};

} // namespace labelqt::services
