#pragma once

#include "services/AutomationService.h"

#include <QString>
#include <QVector>

namespace labelqt::services::AutomationPythonResolver {

QVector<AutomationPythonCommand> candidates(const AutomationPythonSettings& settings);
QString unavailableError(const QVector<AutomationPythonCommand>& candidates, const QString& lastError);

} // namespace labelqt::services::AutomationPythonResolver
