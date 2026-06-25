#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace labelqt::core {

struct ApplicationTheme {
    QString id;
    QString resourcePath;
};

const QVector<ApplicationTheme>& builtInApplicationThemes();
QStringList builtInApplicationThemeIds();
QString applicationThemeResourcePath(const QString& themeId);
bool isBuiltInApplicationTheme(const QString& themeId);

} // namespace labelqt::core
