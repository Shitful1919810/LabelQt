#pragma once

#include "core/Project.h"
#include "core/UndoStack.h"
#include "services/ProjectMergeService.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

namespace labelqt::services {

struct ProjectViewState {
    QString currentImageName;
    int fallbackImageIndex{-1};
    int zoomPercent{100};
    QPointF normalizedCenter{0.5, 0.5};
};

class ProjectWorkflowController {
public:
    using ReplaceImagesCallback = std::function<void(
        QVector<labelqt::core::ImageEntry> images, const QString& preferredImageName, int fallbackImageIndex,
        int zoomPercent, QPointF normalizedCenter, std::optional<QStringList> commentLines)>;
    using DirtyCallback = std::function<void()>;

    ProjectWorkflowController(labelqt::core::Project& project, labelqt::core::UndoStack& undoStack,
                              QString reorderPagesText);

    void setCallbacks(ReplaceImagesCallback replaceImages, DirtyCallback dirty);

    ProjectMergePlan createMergePlan(const QStringList& paths) const;
    labelqt::core::Project mergedProject(ProjectMergePlan mergePlan, const QVector<int>& selectedCandidateIndexes,
                                         const QString& savePath, const QVector<int>& pageOrder) const;
    labelqt::core::Project mergedProjectPreview(const ProjectMergePlan& mergePlan,
                                                const QVector<int>& selectedCandidateIndexes) const;
    void saveProject(const labelqt::core::Project& project, const QString& path) const;
    bool applyPageOrder(const QVector<int>& order, const ProjectViewState& viewState);

private:
    labelqt::core::Project& m_project;
    labelqt::core::UndoStack& m_undoStack;
    QString m_reorderPagesText;
    ReplaceImagesCallback m_replaceImages;
    DirtyCallback m_dirty;
};

} // namespace labelqt::services
