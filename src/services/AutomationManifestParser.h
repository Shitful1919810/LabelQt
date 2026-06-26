#pragma once

#include "services/AutomationService.h"

#include <expected>

#include <QJsonObject>
#include <QMap>
#include <QVector>

namespace labelqt::services::AutomationManifestParser {

QVector<AutomationParameter> parametersFromManifest(const QJsonObject& manifest);
QVector<AutomationSecret> secretsFromManifest(const QJsonObject& manifest);
QMap<QString, QString> environmentFromManifest(const QJsonObject& manifest);
std::expected<AutomationRunResult, QString> tryResultFromOutput(const QJsonObject& output);
AutomationRunResult resultFromOutput(const QJsonObject& output);

} // namespace labelqt::services::AutomationManifestParser
