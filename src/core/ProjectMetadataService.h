#pragma once

#include <QJsonObject>
#include <QStringList>
#include <expected>

namespace labelqt::core {

class ProjectMetadataService final {
public:
    static std::expected<QJsonObject, QString> tryMetadataObject(const QStringList& commentLines);
    static QJsonObject metadataObject(const QStringList& commentLines);
    static QStringList rewriteCommentLines(const QStringList& commentLines, const QJsonObject& metadata);
};

} // namespace labelqt::core
