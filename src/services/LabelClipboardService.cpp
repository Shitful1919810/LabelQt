#include "services/LabelClipboardService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

#include <expected>
#include <optional>

namespace labelqt::services {

namespace {
constexpr QLatin1StringView formatKey{"format"};
constexpr QLatin1StringView versionKey{"version"};
constexpr QLatin1StringView pasteBehaviorKey{"pasteBehavior"};
constexpr QLatin1StringView sourceImageKey{"sourceImage"};
constexpr QLatin1StringView labelsKey{"labels"};
constexpr QLatin1StringView textKey{"text"};
constexpr QLatin1StringView groupKey{"group"};
constexpr QLatin1StringView xKey{"x"};
constexpr QLatin1StringView yKey{"y"};
constexpr QLatin1StringView labelQtLabelsFormat{"LabelQtLabels"};
constexpr QLatin1StringView offsetPasteBehavior{"offset"};
constexpr QLatin1StringView preservePositionPasteBehavior{"preservePosition"};
constexpr int currentVersion = 1;

QString pasteBehaviorToString(ClipboardLabels::PasteBehavior behavior)
{
    return behavior == ClipboardLabels::PasteBehavior::PreservePositionOnPaste ? QString(preservePositionPasteBehavior)
                                                                              : QString(offsetPasteBehavior);
}

ClipboardLabels::PasteBehavior pasteBehaviorFromString(const QString& behavior)
{
    return behavior == QString(preservePositionPasteBehavior) ? ClipboardLabels::PasteBehavior::PreservePositionOnPaste
                                                             : ClipboardLabels::PasteBehavior::OffsetOnPaste;
}

QJsonObject labelToJson(const labelqt::core::Label& label)
{
    const QPointF position = label.position();
    return {
        {QString(textKey), label.text()},
        {QString(groupKey), label.group()},
        {QString(xKey), position.x()},
        {QString(yKey), position.y()},
    };
}

std::optional<labelqt::core::Label> labelFromJson(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue text = object.value(QString(textKey));
    const QJsonValue group = object.value(QString(groupKey));
    const QJsonValue x = object.value(QString(xKey));
    const QJsonValue y = object.value(QString(yKey));
    if (!text.isString() || !group.isString() || !x.isDouble() || !y.isDouble()) {
        return std::nullopt;
    }

    return labelqt::core::Label(text.toString(), group.toString(), QPointF(x.toDouble(), y.toDouble()));
}
} // namespace

QMimeData* LabelClipboardService::createMimeData(const ClipboardLabels& labels)
{
    QJsonArray labelArray;
    QStringList textLines;
    textLines.reserve(labels.labels.size());

    for (const labelqt::core::Label& label : labels.labels) {
        if (label.isDeleted()) {
            continue;
        }
        labelArray.append(labelToJson(label));
        textLines.append(label.text());
    }

    QJsonObject root{
        {QString(formatKey), QString(labelQtLabelsFormat)},
        {QString(versionKey), currentVersion},
        {QString(pasteBehaviorKey), pasteBehaviorToString(labels.pasteBehavior)},
        {QString(sourceImageKey), labels.sourceImageName},
        {QString(labelsKey), labelArray},
    };

    auto* mimeData = new QMimeData;
    mimeData->setData(QString::fromLatin1(mimeType), QJsonDocument(root).toJson(QJsonDocument::Compact));
    mimeData->setText(textLines.join(QStringLiteral("\n\n")));
    return mimeData;
}

std::expected<ClipboardLabels, QString> LabelClipboardService::tryReadMimeData(const QMimeData* mimeData)
{
    if (mimeData == nullptr || !mimeData->hasFormat(QString::fromLatin1(mimeType))) {
        return std::unexpected(QStringLiteral("Clipboard does not contain LabelQt label data."));
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(mimeData->data(QString::fromLatin1(mimeType)), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(
            QStringLiteral("Clipboard label data is not valid JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return std::unexpected(QStringLiteral("Clipboard label data root must be a JSON object."));
    }

    const QJsonObject root = document.object();
    if (root.value(QString(formatKey)).toString() != QString(labelQtLabelsFormat)) {
        return std::unexpected(QStringLiteral("Clipboard label data has an unsupported format."));
    }
    if (root.value(QString(versionKey)).toInt() != currentVersion) {
        return std::unexpected(QStringLiteral("Clipboard label data has an unsupported version."));
    }

    ClipboardLabels result;
    result.sourceImageName = root.value(QString(sourceImageKey)).toString();
    result.pasteBehavior = pasteBehaviorFromString(root.value(QString(pasteBehaviorKey)).toString());
    const QJsonArray labelArray = root.value(QString(labelsKey)).toArray();
    result.labels.reserve(labelArray.size());
    for (const QJsonValue& value : labelArray) {
        if (std::optional<labelqt::core::Label> label = labelFromJson(value); label.has_value()) {
            result.labels.append(std::move(*label));
        }
    }

    return result;
}

ClipboardLabels LabelClipboardService::readMimeData(const QMimeData* mimeData)
{
    return tryReadMimeData(mimeData).value_or(ClipboardLabels{});
}

} // namespace labelqt::services
