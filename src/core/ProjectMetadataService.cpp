#include "core/ProjectMetadataService.h"

#include <QByteArray>
#include <QJsonDocument>

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
    const QString encoded = QString::fromLatin1(qCompress(jsonBytes, 9).toBase64());

    QStringList lines;
    for (qsizetype offset = 0; offset < encoded.size(); offset += encodedPayloadLineLength) {
        const qsizetype lineLength = std::min(encodedPayloadLineLength, encoded.size() - offset);
        lines.append(QStringLiteral("# payload=%1").arg(encoded.sliced(offset, lineLength)));
    }
    return lines;
}

QJsonObject metadataFromPayload(const QString& encodedPayload)
{
    const QByteArray compressedBytes = QByteArray::fromBase64(encodedPayload.toLatin1());
    if (compressedBytes.isEmpty()) {
        return {};
    }

    const QByteArray jsonBytes = qUncompress(compressedBytes);
    if (jsonBytes.isEmpty()) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes);
    return document.isObject() ? document.object() : QJsonObject{};
}
} // namespace

QJsonObject ProjectMetadataService::metadataObject(const QStringList& commentLines)
{
    bool isInMetadataBlock = false;
    QString encodedPayload;

    for (const QString& line : commentLines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(metadataStartMarker)) {
            isInMetadataBlock = true;
            continue;
        }
        if (isInMetadataBlock && trimmedLine.startsWith(metadataEndMarker)) {
            break;
        }
        if (!isInMetadataBlock) {
            continue;
        }

        const QString content = uncommentLine(line);
        if (content.startsWith(QStringLiteral("payload="))) {
            encodedPayload.append(content.sliced(QStringLiteral("payload=").size()).trimmed());
        }
    }

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
