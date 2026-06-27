#pragma once

#include <QString>
#include <QStringList>

namespace labelqt::core {

QString installDataDirectory();
QString userConfigDirectory();
QString userDataDirectory();
QString userAutomationScriptsDirectory();
QString preferenceFilePathForReading();
QString preferenceFilePathForWriting();
QStringList automationScriptsRootCandidates();
QStringList i18nDirectoryCandidates();

} // namespace labelqt::core
