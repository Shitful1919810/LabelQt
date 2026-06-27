#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

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
    QString lastFileDialogDirectory() const;
    void saveLastFileDialogPath(const QString& path) const;
    int labelTableNumberColumnWidth(int defaultWidth) const;
    int labelTableGroupColumnWidth(int defaultWidth) const;
    void saveLabelTableColumnWidths(int numberWidth, int groupWidth) const;
    int pageOrderOriginalIndexColumnWidth(int defaultWidth) const;
    void savePageOrderOriginalIndexColumnWidth(int width) const;
    QVector<int> proofreadingChangesColumnWidths(const QVector<int>& defaultWidths) const;
    void saveProofreadingChangesColumnWidths(const QVector<int>& widths) const;
    bool shouldOpenMergedProjectAfterSave() const;
    void saveShouldOpenMergedProjectAfterSave(bool shouldOpen) const;

private:
    static QString canonicalSessionPath(const QString& path);
    static QString projectSessionGroupName(const QString& path);
};

} // namespace labelqt::services
