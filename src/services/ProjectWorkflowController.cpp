#include "services/ProjectWorkflowController.h"

#include "core/LabelPlusDocument.h"
#include "services/ProjectPageOrderService.h"

#include <utility>

namespace labelqt::services {

ProjectWorkflowController::ProjectWorkflowController(labelqt::core::Project& project,
                                                     labelqt::core::UndoStack& undoStack, QString reorderPagesText)
    : m_project(project), m_undoStack(undoStack), m_reorderPagesText(std::move(reorderPagesText))
{
}

void ProjectWorkflowController::setCallbacks(ReplaceImagesCallback replaceImages, DirtyCallback dirty)
{
    m_replaceImages = std::move(replaceImages);
    m_dirty = std::move(dirty);
}

ProjectMergePlan ProjectWorkflowController::createMergePlan(const QStringList& paths) const
{
    return ProjectMergeService::createPlan(paths);
}

labelqt::core::Project ProjectWorkflowController::mergedProject(ProjectMergePlan mergePlan,
                                                                const QVector<int>& selectedCandidateIndexes,
                                                                const QString& savePath,
                                                                const QVector<int>& pageOrder) const
{
    return ProjectMergeService::mergedProjectWithSelections(std::move(mergePlan), selectedCandidateIndexes, savePath,
                                                            pageOrder);
}

labelqt::core::Project
ProjectWorkflowController::mergedProjectPreview(const ProjectMergePlan& mergePlan,
                                                const QVector<int>& selectedCandidateIndexes) const
{
    return ProjectMergeService::mergedProjectWithSelections(mergePlan, selectedCandidateIndexes);
}

void ProjectWorkflowController::saveProject(const labelqt::core::Project& project, const QString& path) const
{
    labelqt::core::LabelPlusDocument::saveToFile(project, path);
}

bool ProjectWorkflowController::applyPageOrder(const QVector<int>& order, const ProjectViewState& viewState)
{
    if (!ProjectPageOrderService::isValidOrder(order, static_cast<int>(m_project.images().size())) ||
        (order.size() == m_project.images().size() && ProjectPageOrderService::isIdentityOrder(order))) {
        return false;
    }

    const QVector<labelqt::core::ImageEntry> oldImages = m_project.images();
    const QStringList oldCommentLines = m_project.commentLines();

    labelqt::core::Project reorderedProject = m_project;
    ProjectPageOrderService::reorderImages(reorderedProject, order);
    const QVector<labelqt::core::ImageEntry> newImages = reorderedProject.images();
    const QStringList newCommentLines = reorderedProject.commentLines();

    if (m_replaceImages) {
        m_replaceImages(newImages, viewState.currentImageName, viewState.fallbackImageIndex, viewState.zoomPercent,
                        viewState.normalizedCenter, newCommentLines);
    }

    m_undoStack.push(
        m_reorderPagesText, m_reorderPagesText, m_reorderPagesText,
        [this, oldImages, viewState, oldCommentLines]() {
            if (m_replaceImages) {
                m_replaceImages(oldImages, viewState.currentImageName, viewState.fallbackImageIndex,
                                viewState.zoomPercent, viewState.normalizedCenter, oldCommentLines);
            }
            if (m_dirty) {
                m_dirty();
            }
        },
        [this, newImages, viewState, newCommentLines]() {
            if (m_replaceImages) {
                m_replaceImages(newImages, viewState.currentImageName, viewState.fallbackImageIndex,
                                viewState.zoomPercent, viewState.normalizedCenter, newCommentLines);
            }
            if (m_dirty) {
                m_dirty();
            }
        });

    if (m_dirty) {
        m_dirty();
    }
    return true;
}

} // namespace labelqt::services
