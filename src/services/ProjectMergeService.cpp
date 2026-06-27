#include "services/ProjectMergeService.h"

#include "core/LabelPlusDocument.h"
#include "services/PageSourceInfoService.h"
#include "services/ProjectPageOrderService.h"

#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <utility>

namespace labelqt::services {
namespace {
struct LoadedProject {
    QString path;
    labelqt::core::Project project;
};

struct PageMergeData {
    labelqt::core::ImageEntry firstImage;
    int firstProjectIndex{-1};
    QString firstProjectPath;
    QVector<ProjectMergeCandidate> candidates;
};

void appendUniqueGroup(QStringList& groups, const QString& group)
{
    if (!group.isEmpty() && !groups.contains(group)) {
        groups.append(group);
    }
}

QString displayPath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

labelqt::core::ImageEntry withoutDeletedLabels(labelqt::core::ImageEntry image)
{
    image.labels.erase(std::remove_if(image.labels.begin(), image.labels.end(),
                                      [](const labelqt::core::Label& label) { return label.isDeleted(); }),
                       image.labels.end());
    return image;
}

ProjectMergePageSource pageSourceFromCandidate(const QString& imageName, const ProjectMergeCandidate& candidate)
{
    return {imageName, candidate.projectIndex, candidate.projectPath, candidate.labelCount};
}

ProjectMergePageSource pageSourceFromFirstOccurrence(const QString& imageName, const PageMergeData& pageData)
{
    return {imageName, pageData.firstProjectIndex, pageData.firstProjectPath, 0};
}

} // namespace

ProjectMergePlan ProjectMergeService::createPlan(const QStringList& projectPaths)
{
    if (projectPaths.isEmpty()) {
        return {};
    }

    QVector<LoadedProject> loadedProjects;
    loadedProjects.reserve(projectPaths.size());
    for (const QString& path : projectPaths) {
        LoadedProject loaded;
        loaded.path = displayPath(path);
        loaded.project = labelqt::core::LabelPlusDocument::loadFromFile(path);
        loadedProjects.append(std::move(loaded));
    }

    ProjectMergePlan plan;
    QStringList imageOrder;
    QHash<QString, PageMergeData> pageDataByName;
    QStringList mergedGroups;

    for (int projectIndex = 0; projectIndex < loadedProjects.size(); ++projectIndex) {
        const LoadedProject& loaded = loadedProjects.at(projectIndex);
        for (const QString& group : loaded.project.groups()) {
            appendUniqueGroup(mergedGroups, group);
        }
        if (plan.mergedProject.sourceName().isEmpty()) {
            plan.mergedProject.setSourceName(loaded.project.sourceName());
        }

        for (const labelqt::core::ImageEntry& image : loaded.project.images()) {
            const QString imageName = image.name.isEmpty() ? QFileInfo(image.path).fileName() : image.name;
            if (imageName.isEmpty()) {
                continue;
            }

            if (!pageDataByName.contains(imageName)) {
                PageMergeData pageData;
                pageData.firstImage = image;
                pageData.firstImage.name = imageName;
                pageData.firstProjectIndex = projectIndex;
                pageData.firstProjectPath = loaded.path;
                pageDataByName.insert(imageName, std::move(pageData));
                imageOrder.append(imageName);
            }

            const int labelCount = visibleLabelCount(image);
            if (labelCount <= 0) {
                continue;
            }

            ProjectMergeCandidate candidate;
            candidate.projectIndex = projectIndex;
            candidate.projectPath = loaded.path;
            candidate.image = withoutDeletedLabels(image);
            candidate.image.name = imageName;
            candidate.labelCount = labelCount;
            pageDataByName[imageName].candidates.append(std::move(candidate));
        }
    }

    if (mergedGroups.isEmpty()) {
        mergedGroups = {QStringLiteral("框内"), QStringLiteral("框外")};
    }
    plan.mergedProject.setGroups(mergedGroups);

    std::sort(imageOrder.begin(), imageOrder.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });

    for (const QString& imageName : imageOrder) {
        PageMergeData& pageData = pageDataByName[imageName];
        labelqt::core::ImageEntry mergedImage = pageData.firstImage;
        mergedImage.name = imageName;
        mergedImage.labels.clear();

        if (pageData.candidates.size() == 1) {
            mergedImage = pageData.candidates.first().image;
            plan.pageSources.append(pageSourceFromCandidate(imageName, pageData.candidates.first()));
        }
        else if (pageData.candidates.size() > 1) {
            ProjectMergeConflict conflict;
            conflict.imageName = imageName;
            conflict.candidates = pageData.candidates;
            conflict.selectedCandidateIndex = 0;
            plan.conflicts.append(std::move(conflict));
            mergedImage = pageData.candidates.first().image;
            plan.pageSources.append(pageSourceFromCandidate(imageName, pageData.candidates.first()));
        }
        else {
            plan.pageSources.append(pageSourceFromFirstOccurrence(imageName, pageData));
        }

        plan.mergedProject.images().append(std::move(mergedImage));
    }

    return plan;
}

labelqt::core::Project ProjectMergeService::mergedProjectWithSelections(ProjectMergePlan plan,
                                                                           const QVector<int>& selectedCandidateIndexes,
                                                                           const QString& outputProjectPath,
                                                                           const QVector<int>& imageOrder)
{
    QHash<QString, labelqt::core::ImageEntry> selectedImagesByName;
    QHash<QString, ProjectMergePageSource> selectedSourcesByName;
    for (int conflictIndex = 0; conflictIndex < plan.conflicts.size(); ++conflictIndex) {
        ProjectMergeConflict& conflict = plan.conflicts[conflictIndex];
        int selectedCandidateIndex = conflict.selectedCandidateIndex;
        if (conflictIndex < selectedCandidateIndexes.size()) {
            selectedCandidateIndex = selectedCandidateIndexes.at(conflictIndex);
        }
        if (selectedCandidateIndex < 0 || selectedCandidateIndex >= conflict.candidates.size()) {
            selectedCandidateIndex = 0;
        }

        const ProjectMergeCandidate& selectedCandidate = conflict.candidates.at(selectedCandidateIndex);
        labelqt::core::ImageEntry selectedImage = selectedCandidate.image;
        selectedImage.name = conflict.imageName;
        selectedImagesByName.insert(conflict.imageName, std::move(selectedImage));
        selectedSourcesByName.insert(conflict.imageName,
                                     pageSourceFromCandidate(conflict.imageName, selectedCandidate));
    }

    QVector<ProjectMergePageSource> finalPageSources;
    finalPageSources.reserve(plan.mergedProject.images().size());
    for (labelqt::core::ImageEntry& image : plan.mergedProject.images()) {
        const QString imageName = image.name.isEmpty() ? QFileInfo(image.path).fileName() : image.name;
        if (selectedImagesByName.contains(imageName)) {
            image = selectedImagesByName.value(imageName);
        }
        if (selectedSourcesByName.contains(imageName)) {
            finalPageSources.append(selectedSourcesByName.value(imageName));
            continue;
        }
        const auto sourceIt = std::find_if(
            plan.pageSources.cbegin(), plan.pageSources.cend(),
            [&imageName](const ProjectMergePageSource& pageSource) { return pageSource.imageName == imageName; });
        if (sourceIt != plan.pageSources.cend()) {
            finalPageSources.append(*sourceIt);
        }
    }

    if (!imageOrder.isEmpty() &&
        ProjectPageOrderService::isValidOrder(imageOrder, static_cast<int>(plan.mergedProject.images().size())) &&
        (imageOrder.size() != plan.mergedProject.images().size() ||
         !ProjectPageOrderService::isIdentityOrder(imageOrder))) {
        plan.mergedProject.images() = ProjectPageOrderService::reorderedImages(plan.mergedProject.images(), imageOrder);

        QVector<ProjectMergePageSource> reorderedPageSources;
        reorderedPageSources.reserve(finalPageSources.size());
        for (int sourceIndex : imageOrder) {
            reorderedPageSources.append(finalPageSources.at(sourceIndex));
        }
        finalPageSources = std::move(reorderedPageSources);
    }

    plan.mergedProject.setFilePath(outputProjectPath);
    QHash<QString, PageSourceInfo> sourcesByImageName;
    sourcesByImageName.reserve(finalPageSources.size());
    for (const ProjectMergePageSource& source : std::as_const(finalPageSources)) {
        sourcesByImageName.insert(source.imageName, PageSourceInfo{source.projectIndex + 1, source.projectPath});
    }
    PageSourceInfoService::rewriteCommentLinesForCurrentImageOrder(plan.mergedProject, sourcesByImageName);
    return std::move(plan.mergedProject);
}

int ProjectMergeService::visibleLabelCount(const labelqt::core::ImageEntry& image)
{
    int count = 0;
    for (const labelqt::core::Label& label : image.labels) {
        if (!label.isDeleted()) {
            ++count;
        }
    }
    return count;
}

} // namespace labelqt::services
