#include "services/project/PageManagementDraft.h"
#include "services/project/ProjectPageOrderService.h"
#include <algorithm>

namespace labelqt::services {

PageManagementDraft::PageManagementDraft(labelqt::core::Project project) : m_project(std::move(project))
{
    for (int i = 0; i < m_project.images().size(); ++i)
        m_state.order.append(i);
}

const labelqt::core::Project& PageManagementDraft::project() const
{
    return m_project;
}
const QVector<int>& PageManagementDraft::order() const
{
    return m_state.order;
}
const ProjectImageRenamePlan& PageManagementDraft::pending() const
{
    return m_state.pending;
}
const PageManagementDraft::State& PageManagementDraft::state() const
{
    return m_state;
}

void PageManagementDraft::restore(State state)
{
    if (!ProjectPageOrderService::isValidOrder(state.order, static_cast<int>(m_project.images().size())))
        return;
    m_state = std::move(state);
    pruneRemoved();
}

void PageManagementDraft::setOrder(QVector<int> order)
{
    if (!ProjectPageOrderService::isValidOrder(order, static_cast<int>(m_project.images().size())))
        return;
    m_state.order = std::move(order);
    pruneRemoved();
}

void PageManagementDraft::pruneRemoved()
{
    QVector<int> removed;
    for (const auto& item : m_state.pending.items)
        if (!m_state.order.contains(item.imageIndex))
            removed.append(item.imageIndex);
    clearPending(removed);
}

ProjectImageRenamePlan PageManagementDraft::preview(const ProjectImageRenamePlan& incoming) const
{
    auto candidate = incoming;
    candidate.finalPageOrder = m_state.order;
    for (auto& item : candidate.items) {
        if (!m_state.order.contains(item.imageIndex)) {
            item.included = false;
            item.error.clear();
            item.newName = item.oldName;
            item.newPath = item.oldPath;
        }
    }
    return ProjectImageRenameService::mergePlans(m_state.pending, candidate);
}

bool PageManagementDraft::queue(const ProjectImageRenamePlan& incoming)
{
    auto candidate = preview(incoming);
    if (!candidate.isValid())
        return false;
    m_state.pending = std::move(candidate);
    return true;
}

void PageManagementDraft::clearPending(const QVector<int>& indexes)
{
    m_state.pending.finalPageOrder = m_state.order;
    for (auto& item : m_state.pending.items) {
        if (!indexes.contains(item.imageIndex))
            continue;
        item.included = false;
        item.newName = item.oldName;
        item.newPath = item.oldPath;
        item.error.clear();
    }
    m_state.pending = ProjectImageRenameService::mergePlans(m_state.pending, {});
}

ProjectImageRenameItem PageManagementDraft::pendingItem(int index) const
{
    const auto found = std::find_if(m_state.pending.items.begin(), m_state.pending.items.end(),
                                    [index](const auto& item) { return item.imageIndex == index; });
    return found == m_state.pending.items.end() ? ProjectImageRenameItem{} : *found;
}

} // namespace labelqt::services
