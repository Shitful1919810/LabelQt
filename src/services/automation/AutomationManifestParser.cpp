#include "services/automation/AutomationManifestParser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>

namespace labelqt::services::AutomationManifestParser {
namespace {
QString stringFromJsonValue(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return {};
}

} // namespace

QVector<AutomationParameter> parametersFromManifest(const QJsonObject& manifest)
{
    QVector<AutomationParameter> parameters;
    const QJsonArray parameterArray = manifest.value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue& value : parameterArray) {
        const QJsonObject object = value.toObject();
        const QString key = object.value(QStringLiteral("key")).toString().trimmed();
        if (key.isEmpty()) {
            continue;
        }

        AutomationParameter parameter;
        parameter.key = key;
        parameter.label = object.value(QStringLiteral("label")).toString(key);
        parameter.type = object.value(QStringLiteral("type")).toString(QStringLiteral("text"));
        parameter.defaultValue = stringFromJsonValue(object.value(QStringLiteral("default")));
        parameter.secretKey = object.value(QStringLiteral("secretKey")).toString(key);
        parameter.secretService = object.value(QStringLiteral("service")).toString(QStringLiteral("LabelQt"));
        parameter.secretAccount = object.value(QStringLiteral("account")).toString(parameter.secretKey);
        parameter.secretEnvironment = object.value(QStringLiteral("environment")).toString();
        const QJsonArray options = object.value(QStringLiteral("options")).toArray();
        for (const QJsonValue& option : options) {
            const QString optionText = stringFromJsonValue(option).trimmed();
            if (!optionText.isEmpty()) {
                parameter.options.append(optionText);
            }
        }
        parameters.append(parameter);
    }
    return parameters;
}

QVector<AutomationSecret> secretsFromManifest(const QJsonObject& manifest)
{
    QVector<AutomationSecret> secrets;
    const QJsonArray secretArray = manifest.value(QStringLiteral("secrets")).toArray();
    for (const QJsonValue& value : secretArray) {
        const QJsonObject object = value.toObject();
        const QString key = object.value(QStringLiteral("key")).toString().trimmed();
        const QString environment = object.value(QStringLiteral("environment")).toString().trimmed();
        if (key.isEmpty() || environment.isEmpty()) {
            continue;
        }

        AutomationSecret secret;
        secret.key = key;
        secret.label = object.value(QStringLiteral("label")).toString(key);
        secret.service = object.value(QStringLiteral("service")).toString(QStringLiteral("LabelQt"));
        secret.account = object.value(QStringLiteral("account")).toString(key);
        secret.environment = environment;
        secret.required = object.value(QStringLiteral("required")).toBool(true);
        secrets.append(secret);
    }
    return secrets;
}

QMap<QString, QString> environmentFromManifest(const QJsonObject& manifest)
{
    QMap<QString, QString> environment;
    const QJsonObject object = manifest.value(QStringLiteral("environment")).toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString() || it.key().trimmed().isEmpty()) {
            continue;
        }
        environment.insert(it.key(), it.value().toString());
    }
    return environment;
}

} // namespace labelqt::services::AutomationManifestParser
