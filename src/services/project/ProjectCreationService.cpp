#include "ProjectCreationService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>

namespace labelqt::services {

QStringList ProjectCreationService::defaultGroups()
{
    return {QCoreApplication::translate("MainWindow", "Dialogue"),
            QCoreApplication::translate("MainWindow", "Outside text")};
}

QStringList ProjectCreationService::supportedImageNameFilters()
{
    QStringList filters;
    for (const auto& format : QImageReader::supportedImageFormats()) {
        filters.append("*." + QString::fromLatin1(format).toLower());
        filters.append("*." + QString::fromLatin1(format).toUpper());
    }
    filters.removeDuplicates();
    filters.sort(Qt::CaseInsensitive);
    return filters;
}

core::Project ProjectCreationService::fromImages(const QString& projectPath, const QStringList& relativeNames,
                                                 const QStringList& groups, const QString& sourceName)
{
    core::Project project;
    project.setFilePath(projectPath);
    project.setSourceName(sourceName);
    project.setGroups(groups);
    const QDir directory(QFileInfo(projectPath).absolutePath());
    for (const auto& name : relativeNames)
        project.images().append({name, directory.filePath(name), {}});
    return project;
}

void ProjectCreationService::sortImageNames(QStringList& names)
{
    // Android Qt may have no ICU, so QCollator numeric mode is not portable here.
    // Compare ASCII digit runs without integer conversion (also handles very long numbers).
    const auto digit = [](QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); };
    std::sort(names.begin(), names.end(), [&](const QString& left, const QString& right) {
        const auto a = left.toCaseFolded(), b = right.toCaseFolded();
        qsizetype i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (digit(a[i]) && digit(b[j])) {
                auto endA = i, endB = j;
                while (endA < a.size() && digit(a[endA]))
                    ++endA;
                while (endB < b.size() && digit(b[endB]))
                    ++endB;
                while (i < endA && a[i] == QLatin1Char('0'))
                    ++i;
                while (j < endB && b[j] == QLatin1Char('0'))
                    ++j;
                if (endA - i != endB - j)
                    return endA - i < endB - j;
                const auto comparison = QStringView(a).mid(i, endA - i).compare(QStringView(b).mid(j, endB - j));
                if (comparison != 0)
                    return comparison < 0;
                i = endA;
                j = endB;
            }
            else if (a[i] != b[j])
                return a[i] < b[j];
            else {
                ++i;
                ++j;
            }
        }
        return i != a.size() || j != b.size() ? i == a.size() : left < right;
    });
}

} // namespace labelqt::services
