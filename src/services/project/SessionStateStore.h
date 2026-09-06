#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace labelqt::core {
class Project;
}

namespace labelqt::services {

struct WindowLayoutState {
    QByteArray geometry;
    QByteArray windowState;
    QByteArray rootSplitterState;
    QByteArray rightSplitterState;
};

struct ProjectSessionState {
    bool isValid{false};
    QString imageName;
    int imageIndex{0};
    int zoomPercent{100};
    QPointF viewCenter{0.5, 0.5};
    int selectedLabelIndex{-1};
    QVector<int> selectedLabelIndexes;
    QUuid imageId;
    QUuid selectedLabelId;
    QVector<QUuid> selectedLabelIds;
    int resolveImageIndex(const labelqt::core::Project& project) const;
    QVector<int> resolveLabelIndexes(const labelqt::core::Project& project) const;
    int resolvePrimaryLabelIndex(const labelqt::core::Project& project) const;
};

enum class FileDialogScope {
    NewProjectImages,
    OpenProject,
    SaveProject,
    MergeOpenProjects,
    MergeSaveProject,
    CompareProject,
    ExportProofreadingReport,
};

class SessionStateStore {
public:
    WindowLayoutState loadWindowLayout() const;
    void saveWindowLayout(const WindowLayoutState& state) const;

    ProjectSessionState loadProjectSession(const QString& projectPath) const;
    void saveProjectSession(const QString& projectPath, const ProjectSessionState& state) const;
    QStringList recentProjectPaths(int maximumCount = 10) const;
    QString mostRecentProjectPath() const;
    void addRecentProjectPath(const QString& projectPath, int maximumCount = 10) const;
    void removeRecentProjectPath(const QString& projectPath) const;
    QString lastFileDialogDirectory(FileDialogScope scope) const;
    void saveLastFileDialogPath(FileDialogScope scope, const QString& path) const;
    int labelTableNumberColumnWidth(int defaultWidth) const;
    int labelTableGroupColumnWidth(int defaultWidth) const;
    void saveLabelTableColumnWidths(int numberWidth, int groupWidth) const;
    QVector<int> pageOrderColumnWidths(const QVector<int>& defaultWidths) const;
    void savePageOrderColumnWidths(const QVector<int>& widths) const;
    QVector<int> proofreadingChangesColumnWidths(const QVector<int>& defaultWidths) const;
    void saveProofreadingChangesColumnWidths(const QVector<int>& widths) const;
    bool shouldOpenMergedProjectAfterSave() const;
    void saveShouldOpenMergedProjectAfterSave(bool shouldOpen) const;

private:
    static QString canonicalSessionPath(const QString& path);
    static QString projectSessionGroupName(const QString& path);
};

} // namespace labelqt::services
