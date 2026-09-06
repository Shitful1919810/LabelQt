#pragma once

#include "services/automation/AutomationService.h"

#include <QJsonObject>
#include <QMap>
#include <QVector>

namespace labelqt::services::AutomationManifestParser {

QVector<AutomationParameter> parametersFromManifest(const QJsonObject& manifest);
QVector<AutomationSecret> secretsFromManifest(const QJsonObject& manifest);
QMap<QString, QString> environmentFromManifest(const QJsonObject& manifest);

} // namespace labelqt::services::AutomationManifestParser
