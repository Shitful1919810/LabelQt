#include "services/SessionStateStore.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QSettings>
#include <QStringView>

#include <algorithm>

namespace labelqt::services {

namespace {
constexpr QLatin1StringView layoutGroup{"layout"};
constexpr QLatin1StringView geometryKey{"geometry"};
constexpr QLatin1StringView windowStateKey{"windowState"};
constexpr QLatin1StringView rootSplitterKey{"rootSplitter"};
constexpr QLatin1StringView rightSplitterKey{"rightSplitter"};
constexpr QLatin1StringView labelTableNumberColumnWidthKey{"labelTableNumberColumnWidth"};
constexpr QLatin1StringView labelTableGroupColumnWidthKey{"labelTableGroupColumnWidth"};
constexpr QLatin1StringView pageOrderOriginalIndexColumnWidthKey{"pageOrderOriginalIndexColumnWidth"};
constexpr QLatin1StringView proofreadingChangesColumnWidthsKey{"proofreadingChangesColumnWidths"};
constexpr QLatin1StringView projectSessionsGroup{"projectSessions"};
constexpr QLatin1StringView sessionFilePathKey{"filePath"};
constexpr QLatin1StringView sessionImageIndexKey{"imageIndex"};
constexpr QLatin1StringView sessionImageNameKey{"imageName"};
constexpr QLatin1StringView sessionZoomPercentKey{"zoomPercent"};
constexpr QLatin1StringView sessionViewCenterXKey{"viewCenterX"};
constexpr QLatin1StringView sessionViewCenterYKey{"viewCenterY"};
constexpr QLatin1StringView sessionSelectedLabelIndexKey{"selectedLabelIndex"};
constexpr QLatin1StringView sessionSelectedLabelIndexesKey{"selectedLabelIndexes"};
constexpr QLatin1StringView recentProjectsGroup{"recentProjects"};
constexpr QLatin1StringView recentProjectPathsKey{"paths"};
constexpr QLatin1StringView fileDialogsGroup{"fileDialogs"};
constexpr QLatin1StringView lastDirectoryKey{"lastDirectory"};
constexpr QLatin1StringView mergeGroup{"merge"};
constexpr QLatin1StringView openMergedProjectAfterSaveKey{"openMergedProjectAfterSave"};

QLatin1StringView fileDialogScopeKey(FileDialogScope scope)
{
    switch (scope) {
    case FileDialogScope::NewProjectImages:
        return QLatin1StringView{"newProjectImages"};
    case FileDialogScope::OpenProject:
        return QLatin1StringView{"openProject"};
    case FileDialogScope::SaveProject:
        return QLatin1StringView{"saveProject"};
    case FileDialogScope::MergeOpenProjects:
        return QLatin1StringView{"mergeOpenProjects"};
    case FileDialogScope::MergeSaveProject:
        return QLatin1StringView{"mergeSaveProject"};
    case FileDialogScope::CompareProject:
        return QLatin1StringView{"compareProject"};
    case FileDialogScope::ExportProofreadingReport:
        return QLatin1StringView{"exportProofreadingReport"};
    }
    return QLatin1StringView{"default"};
}
} // namespace

WindowLayoutState SessionStateStore::loadWindowLayout() const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    return {
        settings.value(geometryKey).toByteArray(),
        settings.value(windowStateKey).toByteArray(),
        settings.value(rootSplitterKey).toByteArray(),
        settings.value(rightSplitterKey).toByteArray(),
    };
}

void SessionStateStore::saveWindowLayout(const WindowLayoutState& state) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    settings.setValue(geometryKey, state.geometry);
    settings.setValue(windowStateKey, state.windowState);
    settings.setValue(rootSplitterKey, state.rootSplitterState);
    settings.setValue(rightSplitterKey, state.rightSplitterState);
}

ProjectSessionState SessionStateStore::loadProjectSession(const QString& projectPath) const
{
    QSettings settings;
    settings.beginGroup(projectSessionsGroup);
    settings.beginGroup(projectSessionGroupName(projectPath));

    if (settings.value(sessionFilePathKey).toString() != canonicalSessionPath(projectPath)) {
        return {};
    }

    ProjectSessionState state;
    state.isValid = true;
    state.imageIndex = settings.value(sessionImageIndexKey, 0).toInt();
    state.imageName = settings.value(sessionImageNameKey).toString();
    state.zoomPercent = settings.value(sessionZoomPercentKey, 100).toInt();
    state.viewCenter = QPointF(settings.value(sessionViewCenterXKey, 0.5).toDouble(),
                               settings.value(sessionViewCenterYKey, 0.5).toDouble());
    state.selectedLabelIndex = settings.value(sessionSelectedLabelIndexKey, -1).toInt();
    const QVariantList selectedLabelIndexValues = settings.value(sessionSelectedLabelIndexesKey).toList();
    state.selectedLabelIndexes.reserve(selectedLabelIndexValues.size());
    for (const QVariant& value : selectedLabelIndexValues) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok && index >= 0 && !state.selectedLabelIndexes.contains(index)) {
            state.selectedLabelIndexes.append(index);
        }
    }
    if (state.selectedLabelIndexes.isEmpty() && state.selectedLabelIndex >= 0) {
        state.selectedLabelIndexes.append(state.selectedLabelIndex);
    }
    return state;
}

void SessionStateStore::saveProjectSession(const QString& projectPath, const ProjectSessionState& state) const
{
    if (projectPath.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(projectSessionsGroup);
    settings.beginGroup(projectSessionGroupName(projectPath));
    settings.setValue(sessionFilePathKey, canonicalSessionPath(projectPath));
    settings.setValue(sessionImageIndexKey, state.imageIndex);
    settings.setValue(sessionImageNameKey, state.imageName);
    settings.setValue(sessionZoomPercentKey, state.zoomPercent);
    settings.setValue(sessionViewCenterXKey, state.viewCenter.x());
    settings.setValue(sessionViewCenterYKey, state.viewCenter.y());
    settings.setValue(sessionSelectedLabelIndexKey, state.selectedLabelIndex);
    QVariantList selectedLabelIndexValues;
    selectedLabelIndexValues.reserve(state.selectedLabelIndexes.size());
    for (int index : state.selectedLabelIndexes) {
        if (index >= 0) {
            selectedLabelIndexValues.append(index);
        }
    }
    settings.setValue(sessionSelectedLabelIndexesKey, selectedLabelIndexValues);
    settings.sync();
}

QStringList SessionStateStore::recentProjectPaths(int maximumCount) const
{
    QSettings settings;
    settings.beginGroup(recentProjectsGroup);
    QStringList paths = settings.value(recentProjectPathsKey).toStringList();
    paths.erase(
        std::remove_if(paths.begin(), paths.end(), [](const QString& path) { return path.trimmed().isEmpty(); }),
        paths.end());
    paths.removeDuplicates();
    if (maximumCount > 0 && paths.size() > maximumCount) {
        paths.erase(paths.begin() + maximumCount, paths.end());
    }
    return paths;
}

QString SessionStateStore::mostRecentProjectPath() const
{
    const QStringList paths = recentProjectPaths(1);
    return paths.isEmpty() ? QString() : paths.first();
}

void SessionStateStore::addRecentProjectPath(const QString& projectPath, int maximumCount) const
{
    if (projectPath.isEmpty()) {
        return;
    }

    const QString path = canonicalSessionPath(projectPath);
    QStringList paths = recentProjectPaths(maximumCount);
    paths.removeAll(path);
    paths.prepend(path);
    if (maximumCount > 0 && paths.size() > maximumCount) {
        paths.erase(paths.begin() + maximumCount, paths.end());
    }

    QSettings settings;
    settings.beginGroup(recentProjectsGroup);
    settings.setValue(recentProjectPathsKey, paths);
}

void SessionStateStore::removeRecentProjectPath(const QString& projectPath) const
{
    if (projectPath.isEmpty()) {
        return;
    }

    const QString path = canonicalSessionPath(projectPath);
    QStringList paths = recentProjectPaths();
    paths.removeAll(path);

    QSettings settings;
    settings.beginGroup(recentProjectsGroup);
    settings.setValue(recentProjectPathsKey, paths);
}

QString SessionStateStore::lastFileDialogDirectory(FileDialogScope scope) const
{
    QSettings settings;
    settings.beginGroup(fileDialogsGroup);
    settings.beginGroup(fileDialogScopeKey(scope));
    const QString path = settings.value(lastDirectoryKey).toString();
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(path);
    return fileInfo.isDir() ? fileInfo.absoluteFilePath() : fileInfo.absolutePath();
}

void SessionStateStore::saveLastFileDialogPath(FileDialogScope scope, const QString& path) const
{
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(path);
    const QString directory = fileInfo.isDir() ? fileInfo.absoluteFilePath() : fileInfo.absolutePath();
    if (directory.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(fileDialogsGroup);
    settings.beginGroup(fileDialogScopeKey(scope));
    settings.setValue(lastDirectoryKey, directory);
}

int SessionStateStore::labelTableNumberColumnWidth(int defaultWidth) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    return settings.value(labelTableNumberColumnWidthKey, defaultWidth).toInt();
}

int SessionStateStore::labelTableGroupColumnWidth(int defaultWidth) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    return settings.value(labelTableGroupColumnWidthKey, defaultWidth).toInt();
}

void SessionStateStore::saveLabelTableColumnWidths(int numberWidth, int groupWidth) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    settings.setValue(labelTableNumberColumnWidthKey, numberWidth);
    settings.setValue(labelTableGroupColumnWidthKey, groupWidth);
}

int SessionStateStore::pageOrderOriginalIndexColumnWidth(int defaultWidth) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    return settings.value(pageOrderOriginalIndexColumnWidthKey, defaultWidth).toInt();
}

void SessionStateStore::savePageOrderOriginalIndexColumnWidth(int width) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    settings.setValue(pageOrderOriginalIndexColumnWidthKey, width);
}

QVector<int> SessionStateStore::proofreadingChangesColumnWidths(const QVector<int>& defaultWidths) const
{
    QSettings settings;
    settings.beginGroup(layoutGroup);
    const QVariantList values = settings.value(proofreadingChangesColumnWidthsKey).toList();
    if (values.size() != defaultWidths.size()) {
        return defaultWidths;
    }

    QVector<int> widths;
    widths.reserve(values.size());
    for (int i = 0; i < values.size(); ++i) {
        bool ok = false;
        const int width = values.at(i).toInt(&ok);
        widths.append(ok && width > 0 ? width : defaultWidths.at(i));
    }
    return widths;
}

void SessionStateStore::saveProofreadingChangesColumnWidths(const QVector<int>& widths) const
{
    QVariantList values;
    values.reserve(widths.size());
    for (int width : widths) {
        values.append(width);
    }

    QSettings settings;
    settings.beginGroup(layoutGroup);
    settings.setValue(proofreadingChangesColumnWidthsKey, values);
}

bool SessionStateStore::shouldOpenMergedProjectAfterSave() const
{
    QSettings settings;
    settings.beginGroup(mergeGroup);
    return settings.value(openMergedProjectAfterSaveKey, true).toBool();
}

void SessionStateStore::saveShouldOpenMergedProjectAfterSave(bool shouldOpen) const
{
    QSettings settings;
    settings.beginGroup(mergeGroup);
    settings.setValue(openMergedProjectAfterSaveKey, shouldOpen);
}

QString SessionStateStore::canonicalSessionPath(const QString& path)
{
    const QFileInfo fileInfo(path);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    return canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath;
}

QString SessionStateStore::projectSessionGroupName(const QString& path)
{
    const QByteArray hash =
        QCryptographicHash::hash(canonicalSessionPath(path).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash);
}

} // namespace labelqt::services
