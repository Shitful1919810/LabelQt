#pragma once

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <expected>

namespace labelqt::services::BoundedJsonIo {

inline constexpr qint64 MaximumBytes = 64 * 1024 * 1024;
inline constexpr qint64 SnapshotBytes = 16 * 1024 * 1024;
inline constexpr qsizetype RecordItems = 10000;
struct Limits {
    qint64 bytes;
    qsizetype items;
    int depth{32};
};
inline constexpr Limits Backup{64 * 1024, 64};
inline constexpr Limits Rename{MaximumBytes, RecordItems * 8 + 16};
inline constexpr Limits Review{16 * 1024 * 1024, RecordItems * 18 + 16};

inline bool validLimits(Limits limits)
{
    return limits.bytes > 0 && limits.bytes <= MaximumBytes && limits.items > 0 && limits.items <= 1000000 &&
           limits.depth > 0 && limits.depth <= 64;
}

inline bool writableFile(const QString& path, bool newOnly = false)
{
    const QFileInfo info(path);
    return !path.isEmpty() && !info.isSymLink() && (!info.exists() || (!newOnly && info.isFile()));
}

inline std::expected<QByteArray, QString> readBytes(const QString& path, qint64 maximum, const QString& error)
{
    const QFileInfo info(path);
    if (maximum <= 0 || maximum > MaximumBytes || info.isSymLink() || !info.isFile() || info.size() > maximum)
        return std::unexpected(error);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(file.errorString());
    if (file.isSequential() || file.size() > maximum)
        return std::unexpected(error);
    auto bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximum || !file.atEnd())
        return std::unexpected(error);
    return bytes;
}

inline bool checkValue(const QJsonValue& value, Limits limits, qsizetype& remaining, int depth = 0)
{
    if (--remaining < 0 || depth > limits.depth)
        return false;
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!checkValue(it.value(), limits, remaining, depth + 1))
                return false;
    }
    else if (value.isArray()) {
        for (const auto& item : value.toArray())
            if (!checkValue(item, limits, remaining, depth + 1))
                return false;
    }
    return true;
}

inline std::expected<QJsonObject, QString> parse(const QByteArray& bytes, Limits limits, const QString& error)
{
    if (!validLimits(limits) || bytes.size() > limits.bytes)
        return std::unexpected(error);
    // Bound nesting before Qt allocates the document; syntax remains Qt's responsibility.
    bool string = false, escape = false;
    int depth = 0;
    qsizetype separators = 0;
    for (const char ch : bytes) {
        if (string) {
            if (escape)
                escape = false;
            else if (ch == '\\')
                escape = true;
            else if (ch == '"')
                string = false;
        }
        else if (ch == '"')
            string = true;
        else if (ch == '{' || ch == '[') {
            if (++depth > limits.depth)
                return std::unexpected(error);
        }
        else if (ch == '}' || ch == ']')
            --depth;
        else if ((ch == ',' || ch == ':') && ++separators > limits.items * 2)
            return std::unexpected(error);
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    auto remaining = limits.items;
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !checkValue(document.object(), limits, remaining, 1))
        return std::unexpected(error);
    return document.object();
}

inline std::expected<QJsonObject, QString> read(const QString& path, Limits limits, const QString& error)
{
    if (!validLimits(limits))
        return std::unexpected(error);
    const auto bytes = readBytes(path, limits.bytes, error);
    if (!bytes)
        return std::unexpected(bytes.error());
    return parse(*bytes, limits, error);
}

inline std::expected<QByteArray, QString> encode(const QJsonObject& object, Limits limits, const QString& error)
{
    auto remaining = limits.items;
    if (!validLimits(limits) || !checkValue(object, limits, remaining, 1))
        return std::unexpected(error);
    auto bytes = QJsonDocument(object).toJson();
    if (bytes.size() > limits.bytes)
        return std::unexpected(error);
    return bytes;
}

inline bool isInteger(const QJsonValue& value, int minimum, int maximum)
{
    return value.isDouble() && value.toDouble() >= minimum && value.toDouble() <= maximum &&
           value.toDouble() == value.toInt();
}

inline bool isDigest(const QJsonValue& value)
{
    const auto text = value.toString();
    if (text.size() != 64)
        return false;
    for (const auto ch : text)
        if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f') || (ch >= u'A' && ch <= u'F')))
            return false;
    return true;
}

} // namespace labelqt::services::BoundedJsonIo
