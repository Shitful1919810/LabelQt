#include "core/ProjectMetadataService.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QtEndian>
#include <stdexcept>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <optional>

namespace labelqt::core {
namespace {
constexpr QLatin1StringView metadataStartMarker{"# LabelQtMetadata"};
constexpr QLatin1StringView metadataEndMarker{"# EndLabelQtMetadata"};
constexpr qsizetype encodedPayloadLineLength = 120;

constexpr std::array obsoleteStandaloneStartMarkers{
    QLatin1StringView{"# LabelQtMergeSources"},
    QLatin1StringView{"# LabelQtReview"},
};

constexpr std::array obsoleteStandaloneEndMarkers{
    QLatin1StringView{"# EndLabelQtMergeSources"},
    QLatin1StringView{"# EndLabelQtReview"},
};

QString uncommentLine(QString line)
{
    line = line.trimmed();
    if (line.startsWith(QLatin1Char('#'))) {
        line.remove(0, 1);
    }
    return line.trimmed();
}

std::optional<std::size_t> obsoleteStandaloneBlockIndex(const QString& trimmedLine)
{
    for (std::size_t i = 0; i < obsoleteStandaloneStartMarkers.size(); ++i) {
        if (trimmedLine.startsWith(obsoleteStandaloneStartMarkers.at(i))) {
            return i;
        }
    }
    return std::nullopt;
}

QStringList withoutMetadataBlocks(const QStringList& commentLines)
{
    QStringList filteredLines;
    bool isInMetadataBlock = false;
    std::optional<std::size_t> obsoleteStandaloneBlock;

    for (const QString& line : commentLines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(metadataStartMarker)) {
            isInMetadataBlock = true;
            continue;
        }
        if (isInMetadataBlock) {
            if (trimmedLine.startsWith(metadataEndMarker)) {
                isInMetadataBlock = false;
            }
            continue;
        }

        if (!obsoleteStandaloneBlock.has_value()) {
            obsoleteStandaloneBlock = obsoleteStandaloneBlockIndex(trimmedLine);
            if (obsoleteStandaloneBlock.has_value()) {
                continue;
            }
        } else {
            if (trimmedLine.startsWith(obsoleteStandaloneEndMarkers.at(*obsoleteStandaloneBlock))) {
                obsoleteStandaloneBlock.reset();
            }
            continue;
        }

        filteredLines.append(line);
    }
    return filteredLines;
}

QStringList encodedPayloadLines(const QJsonObject& metadata)
{
    const QByteArray jsonBytes = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    const auto tooLarge = [] {
        throw std::runtime_error(
            QCoreApplication::translate("ProjectMetadataService", "Project metadata exceeds the size limit.")
                .toStdString());
    };
    if (jsonBytes.size() > ProjectMetadataService::maxDecodedBytes)
        tooLarge();
    const auto compressed = qCompress(jsonBytes, 9);
    if (compressed.size() > ProjectMetadataService::maxCompressedBytes)
        tooLarge();
    const QString encoded = QString::fromLatin1(compressed.toBase64());

    QStringList lines;
    for (qsizetype offset = 0; offset < encoded.size(); offset += encodedPayloadLineLength) {
        const qsizetype lineLength = std::min(encodedPayloadLineLength, encoded.size() - offset);
        lines.append(QStringLiteral("# payload=%1").arg(encoded.sliced(offset, lineLength)));
    }
    return lines;
}

std::expected<QJsonObject, QString> invalidMetadata()
{
    return std::unexpected(QCoreApplication::translate(
        "ProjectMetadataService",
        "Project metadata is damaged or uses an unsupported format. The original file has not been changed."));
}

std::expected<QJsonObject, QString> metadataFromPayload(const QString& encodedPayload)
{
    if (encodedPayload.size() > ProjectMetadataService::maxEncodedCharacters)
        return invalidMetadata();
    const QByteArray compressedBytes =
        QByteArray::fromBase64(encodedPayload.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (compressedBytes.size() <= 4 || compressedBytes.size() > ProjectMetadataService::maxCompressedBytes) {
        return invalidMetadata();
    }

    const auto declaredSize = qFromBigEndian<quint32>(compressedBytes.constData());
    if (declaredSize > ProjectMetadataService::maxDecodedBytes)
        return invalidMetadata();
    // qCompress's length prefix is untrusted. Bound actual output as well as the declared size.
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return invalidMetadata();
    const auto cleanup = qScopeGuard([&] { inflateEnd(&stream); });
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressedBytes.constData() + 4));
    stream.avail_in = static_cast<uInt>(compressedBytes.size() - 4);
    QByteArray jsonBytes;
    std::array<char, 65536> buffer;
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            return invalidMetadata();
        const auto produced = static_cast<qsizetype>(buffer.size() - stream.avail_out);
        if (produced > static_cast<qsizetype>(declaredSize) - jsonBytes.size())
            return invalidMetadata();
        jsonBytes.append(buffer.data(), produced);
    } while (status != Z_STREAM_END);
    if (stream.avail_in != 0 || jsonBytes.size() != declaredSize)
        return invalidMetadata();
    if (jsonBytes.isEmpty()) {
        return invalidMetadata();
    }

    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes);
    if (!document.isObject())
        return invalidMetadata();
    return document.object();
}
} // namespace

QJsonObject ProjectMetadataService::metadataObject(const QStringList& commentLines)
{
    return tryMetadataObject(commentLines).value_or(QJsonObject{});
}

std::expected<QJsonObject, QString> ProjectMetadataService::tryMetadataObject(const QStringList& commentLines)
{
    bool isInMetadataBlock = false;
    bool hasMetadataBlock = false;
    bool hasEnd = false;
    QString encodedPayload;

    for (const QString& line : commentLines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(metadataStartMarker)) {
            if (hasMetadataBlock || trimmedLine != QStringLiteral("# LabelQtMetadata v1"))
                return invalidMetadata();
            isInMetadataBlock = true;
            hasMetadataBlock = true;
            continue;
        }
        if (isInMetadataBlock && trimmedLine.startsWith(metadataEndMarker)) {
            hasEnd = true;
            isInMetadataBlock = false;
            continue;
        }
        if (!isInMetadataBlock) {
            continue;
        }

        const QString content = uncommentLine(line);
        if (content.startsWith(QStringLiteral("payload="))) {
            const auto part = content.sliced(QStringLiteral("payload=").size()).trimmed();
            if (part.size() > ProjectMetadataService::maxEncodedCharacters - encodedPayload.size())
                return invalidMetadata();
            encodedPayload.append(part);
        }
        else if ((content.startsWith(QStringLiteral("compression=")) &&
                  content != QStringLiteral("compression=qCompress")) ||
                 (content.startsWith(QStringLiteral("encoding=")) && content != QStringLiteral("encoding=base64"))) {
            return invalidMetadata();
        }
    }
    if (!hasMetadataBlock)
        return QJsonObject{};
    if (!hasEnd)
        return invalidMetadata();
    return metadataFromPayload(encodedPayload);
}

QStringList ProjectMetadataService::rewriteCommentLines(const QStringList& commentLines, const QJsonObject& metadata)
{
    QStringList lines = withoutMetadataBlocks(commentLines);
    if (metadata.isEmpty()) {
        return lines;
    }

    if (!lines.isEmpty() && !lines.last().isEmpty()) {
        lines.append(QString());
    }
    lines.append(QStringLiteral("# LabelQtMetadata v1"));
    lines.append(QStringLiteral("# compression=qCompress"));
    lines.append(QStringLiteral("# encoding=base64"));
    lines.append(encodedPayloadLines(metadata));
    lines.append(QStringLiteral("# EndLabelQtMetadata"));
    return lines;
}

} // namespace labelqt::core
