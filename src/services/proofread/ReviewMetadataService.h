#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace labelqt::services {

struct ReviewLabelSnapshot {
    QString imageName;
    QString text;
    QString group;
    QPointF position;
    int labelIndex{-1};
    QUuid id{};
};

class ReviewMetadata {
public:
    bool hasBaseline() const noexcept;
    void setHasBaseline(bool hasBaseline) noexcept;

    const QHash<QString, ReviewLabelSnapshot>& baselineLabels() const noexcept;

    ReviewLabelSnapshot baselineFor(const QString& imageName, int labelIndex) const;
    void setBaseline(const QString& imageName, int labelIndex, ReviewLabelSnapshot snapshot);
    void clear();

private:
    bool m_hasBaseline{false};
    QHash<QString, ReviewLabelSnapshot> m_baselineLabels;
};

enum class ReviewChangeKind {
    Added,
    Deleted,
    Modified,
    Comment,
};

struct ReviewChange {
    QString imageName;
    QString baselineImageName;
    int imageIndex{-1};
    int labelIndex{-1};
    int baselineLabelIndex{-1};
    int currentLabelIndex{-1};
    ReviewChangeKind kind{ReviewChangeKind::Modified};
    ReviewLabelSnapshot baseline;
    ReviewLabelSnapshot current;
    bool orderChanged{false};
    bool textChanged{false};
    bool groupChanged{false};
    bool positionChanged{false};
    bool manuallyMatched{false};
    QString noteText{};
    QByteArray noteRevision{};
    QUuid pageId{};
    bool jumpAvailable{true};

    QString pageName() const
    {
        if (!imageName.isEmpty())
            return imageName;
        if (!baselineImageName.isEmpty())
            return baselineImageName;
        return current.imageName; // Retain the display name of a detached comment, not an image lookup target.
    }
};

class ReviewMetadataService final {
public:
    static QString keyForLabel(const QString& imageName, int labelIndex);
    static bool projectHasBaseline(const labelqt::core::Project& project);
    static ReviewMetadata captureBaseline(const labelqt::core::Project& project);
    static ReviewMetadata metadataForProject(const labelqt::core::Project& project);
    static QVector<ReviewChange> changesForProject(const labelqt::core::Project& project,
                                                   const ReviewMetadata& metadata);
    static QVector<labelqt::core::Label> baselineImageLabels(const labelqt::core::Project& project,
                                                             const ReviewMetadata& metadata, const QString& imageName);
    static QStringList rewriteCommentLines(const QStringList& commentLines, const ReviewMetadata& metadata);
};

} // namespace labelqt::services
