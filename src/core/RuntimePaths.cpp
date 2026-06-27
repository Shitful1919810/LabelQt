#include "core/RuntimePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace labelqt::core {

namespace {
QString applicationDirectory()
{
    return QCoreApplication::applicationDirPath();
}

QString configuredInstallDataDirectory()
{
#ifdef LABELQT_INSTALL_DATADIR
    return QString::fromUtf8(LABELQT_INSTALL_DATADIR).trimmed();
#else
    return {};
#endif
}

QString relativeInstallDataDirectory()
{
    return QDir(applicationDirectory()).filePath(QStringLiteral("../share/labelqt"));
}

void appendUniquePath(QStringList& paths, const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return;
    }

    const QString cleanPath = QDir::cleanPath(trimmedPath);
    if (!paths.contains(cleanPath)) {
        paths.append(cleanPath);
    }
}

QString preferencePathInDirectory(const QString& directory)
{
    return QDir(directory).filePath(QStringLiteral("preference.json"));
}

QString applicationPreferenceFilePath()
{
    return preferencePathInDirectory(applicationDirectory());
}

QString userPreferenceFilePath()
{
    return preferencePathInDirectory(userConfigDirectory());
}

QString firstExistingFile(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString firstExistingDirectory(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isDir()) {
            return candidate;
        }
    }
    return {};
}

void appendChildDirectory(QStringList& paths, const QString& directory, const QString& childName)
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    appendUniquePath(paths, QDir(directory).filePath(childName));
}

QString applicationScriptsDirectory()
{
    return QDir(applicationDirectory()).filePath(QStringLiteral("scripts"));
}

QString installedScriptsDirectory(const QString& dataDirectory)
{
    return QDir(dataDirectory).filePath(QStringLiteral("scripts"));
}

QString installedOfficialScriptsDirectory(const QString& dataDirectory)
{
    return QDir(installedScriptsDirectory(dataDirectory)).filePath(QStringLiteral("official"));
}

QString applicationOfficialScriptsDirectory()
{
    return QDir(applicationScriptsDirectory()).filePath(QStringLiteral("official"));
}

QString applicationCustomScriptsDirectory()
{
    return QDir(applicationScriptsDirectory()).filePath(QStringLiteral("custom"));
}

} // namespace

QString installDataDirectory()
{
    return configuredInstallDataDirectory();
}

QString userConfigDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString userDataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString userAutomationScriptsDirectory()
{
    return QDir(userDataDirectory()).filePath(QStringLiteral("scripts/custom"));
}

QString preferenceFilePathForReading()
{
    const QString existingPath =
        firstExistingFile({applicationPreferenceFilePath(), userPreferenceFilePath()});
    return existingPath.isEmpty() ? preferenceFilePathForWriting() : existingPath;
}

QString preferenceFilePathForWriting()
{
    const QString applicationPath = applicationPreferenceFilePath();
    if (QFileInfo::exists(applicationPath)) {
        return applicationPath;
    }
    return userPreferenceFilePath();
}

QStringList automationScriptsRootCandidates()
{
    QStringList paths;

    const QString officialScriptsPath = firstExistingDirectory({
        applicationOfficialScriptsDirectory(),
        installedOfficialScriptsDirectory(relativeInstallDataDirectory()),
        installedOfficialScriptsDirectory(installDataDirectory()),
    });
    if (!officialScriptsPath.isEmpty()) {
        appendUniquePath(paths, officialScriptsPath);
    }

    const QString customScriptsPath = firstExistingDirectory({applicationCustomScriptsDirectory()});
    appendUniquePath(paths, customScriptsPath.isEmpty() ? userAutomationScriptsDirectory() : customScriptsPath);
    return paths;
}

QStringList i18nDirectoryCandidates()
{
    QStringList paths;
    appendUniquePath(paths, QStringLiteral(":/i18n"));
    appendChildDirectory(paths, applicationDirectory(), QStringLiteral("i18n"));
    appendChildDirectory(paths, relativeInstallDataDirectory(), QStringLiteral("i18n"));
    appendChildDirectory(paths, installDataDirectory(), QStringLiteral("i18n"));
    return paths;
}

} // namespace labelqt::core
