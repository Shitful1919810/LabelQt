#include "services/ReviewChangeClassifier.h"

#include "services/TextDiffService.h"

#include <QChar>
#include <QSet>

namespace labelqt::services {
namespace {

bool isFormatCharacter(QChar ch)
{
    if (ch.isSpace()) {
        return true;
    }

    switch (ch.category()) {
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
        return true;
    default:
        return false;
    }
}

void addTextFacets(const QString& beforeText, const QString& afterText, QSet<ReviewChangeFacet>& facets)
{
    const QVector<TextDiffChunk> chunks = TextDiffService::diff(beforeText, afterText);
    for (const TextDiffChunk& chunk : chunks) {
        if (chunk.operation == TextDiffOperation::Equal) {
            continue;
        }
        for (QChar ch : chunk.text) {
            facets.insert(isFormatCharacter(ch) ? ReviewChangeFacet::Format : ReviewChangeFacet::Text);
        }
    }
}

void appendIfPresent(QVector<ReviewChangeFacet>& orderedFacets, const QSet<ReviewChangeFacet>& facets,
                     ReviewChangeFacet facet)
{
    if (facets.contains(facet)) {
        orderedFacets.append(facet);
    }
}

} // namespace

QVector<ReviewChangeFacet> ReviewChangeClassifier::facets(const ReviewChange& change)
{
    if (change.kind != ReviewChangeKind::Modified) {
        return {};
    }

    QSet<ReviewChangeFacet> facetSet;
    if (change.textChanged) {
        addTextFacets(change.baseline.text, change.current.text, facetSet);
    }
    if (change.groupChanged) {
        facetSet.insert(ReviewChangeFacet::Group);
    }
    if (change.positionChanged) {
        facetSet.insert(ReviewChangeFacet::Marker);
    }
    if (change.orderChanged) {
        facetSet.insert(ReviewChangeFacet::Order);
    }

    QVector<ReviewChangeFacet> orderedFacets;
    orderedFacets.reserve(facetSet.size());
    appendIfPresent(orderedFacets, facetSet, ReviewChangeFacet::Text);
    appendIfPresent(orderedFacets, facetSet, ReviewChangeFacet::Format);
    appendIfPresent(orderedFacets, facetSet, ReviewChangeFacet::Group);
    appendIfPresent(orderedFacets, facetSet, ReviewChangeFacet::Marker);
    appendIfPresent(orderedFacets, facetSet, ReviewChangeFacet::Order);
    return orderedFacets;
}

QString ReviewChangeClassifier::facetKey(ReviewChangeFacet facet)
{
    switch (facet) {
    case ReviewChangeFacet::Text:
        return QStringLiteral("text");
    case ReviewChangeFacet::Format:
        return QStringLiteral("format");
    case ReviewChangeFacet::Group:
        return QStringLiteral("group");
    case ReviewChangeFacet::Marker:
        return QStringLiteral("marker");
    case ReviewChangeFacet::Order:
        return QStringLiteral("order");
    }
    return {};
}

QString ReviewChangeClassifier::kindKey(ReviewChangeKind kind)
{
    switch (kind) {
    case ReviewChangeKind::Added:
        return QStringLiteral("added");
    case ReviewChangeKind::Deleted:
        return QStringLiteral("deleted");
    case ReviewChangeKind::Modified:
        return QStringLiteral("modified");
    }
    return {};
}

} // namespace labelqt::services
