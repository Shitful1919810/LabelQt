#pragma once

#include <QJsonObject>
#include <QStringList>
#include <expected>

namespace labelqt::core {

class ProjectMetadataService final {
public:
    static constexpr qsizetype maxDecodedBytes = 32 * 1024 * 1024;
    static constexpr qsizetype maxCompressedBytes = 16 * 1024 * 1024;
    static constexpr qsizetype maxEncodedCharacters = ((maxCompressedBytes + 2) / 3) * 4;
    static std::expected<QJsonObject, QString> tryMetadataObject(const QStringList& commentLines);
    static QJsonObject metadataObject(const QStringList& commentLines);
    static QStringList rewriteCommentLines(const QStringList& commentLines, const QJsonObject& metadata);
};

} // namespace labelqt::core
