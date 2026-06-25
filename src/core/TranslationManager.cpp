#include "core/TranslationManager.h"

#include <QApplication>
#include <QDir>
#include <QLocale>
#include <QSet>
#include <QTranslator>

#include <algorithm>

namespace labelqt::core {

namespace {
constexpr auto translationPrefix = "labelqt_";
constexpr auto translationSuffix = ".qm";

QString i18nDirectoryPath()
{
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("i18n"));
}

QString localeNameFromTranslationFile(const QString& fileName)
{
    if (!fileName.startsWith(QLatin1StringView(translationPrefix)) ||
        !fileName.endsWith(QLatin1StringView(translationSuffix))) {
        return {};
    }

    QString localeName = fileName;
    localeName.remove(0, static_cast<int>(std::char_traits<char>::length(translationPrefix)));
    localeName.chop(static_cast<int>(std::char_traits<char>::length(translationSuffix)));
    return localeName.trimmed();
}

void collectLocalesFromDirectory(const QString& path, QSet<QString>& locales)
{
    const QDir directory(path);
    const QStringList fileNames = directory.entryList({QStringLiteral("labelqt_*.qm")}, QDir::Files, QDir::Name);
    for (const QString& fileName : fileNames) {
        const QString localeName = localeNameFromTranslationFile(fileName);
        if (!localeName.isEmpty()) {
            locales.insert(localeName);
        }
    }
}

QString displayNameForLocale(const QString& localeName)
{
    const QLocale locale(localeName);
    QString displayName = locale.nativeLanguageName();
    if (displayName.isEmpty()) {
        displayName = localeName;
    }

    const QString territory = locale.nativeTerritoryName();
    if (!territory.isEmpty()) {
        displayName += QStringLiteral(" - ") + territory;
    }
    return QStringLiteral("%1 (%2)").arg(displayName, localeName);
}
} // namespace

QVector<ApplicationLanguage> availableApplicationLanguages()
{
    QSet<QString> locales;
    collectLocalesFromDirectory(QStringLiteral(":/i18n"), locales);
    collectLocalesFromDirectory(i18nDirectoryPath(), locales);

    QVector<ApplicationLanguage> languages;
    languages.reserve(locales.size());
    for (const QString& localeName : std::as_const(locales)) {
        languages.append({localeName, displayNameForLocale(localeName)});
    }

    std::sort(languages.begin(), languages.end(), [](const ApplicationLanguage& lhs, const ApplicationLanguage& rhs) {
        return QString::localeAwareCompare(lhs.displayName, rhs.displayName) < 0;
    });
    return languages;
}

bool loadApplicationTranslator(QTranslator& translator, const QString& localeName)
{
    const QLocale locale = localeName.trimmed().isEmpty() ? QLocale() : QLocale(localeName.trimmed());
    return translator.load(locale, QStringLiteral("labelqt"), QStringLiteral("_"), QStringLiteral(":/i18n")) ||
           translator.load(locale, QStringLiteral("labelqt"), QStringLiteral("_"), i18nDirectoryPath());
}

} // namespace labelqt::core
