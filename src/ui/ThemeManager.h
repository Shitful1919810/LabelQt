#pragma once

#include <QString>
#include <QStringList>

namespace labelqt::ui {

QStringList availableApplicationThemes();
bool applyApplicationTheme(const QString& themeName);

} // namespace labelqt::ui
