#pragma once

#include <QJsonObject>
#include <QStringList>

namespace labelqt::core {

class ProjectMetadataService final {
public:
    static QJsonObject metadataObject(const QStringList& commentLines);
    static QStringList rewriteCommentLines(const QStringList& commentLines, const QJsonObject& metadata);
};

} // namespace labelqt::core
