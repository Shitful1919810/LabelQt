#pragma once

#include "services/project/ProjectImageRenameService.h"

namespace labelqt::services {

class PageManagementDraft final {
public:
    struct State {
        QVector<int> order;
        ProjectImageRenamePlan pending;
        bool operator==(const State&) const = default;
    };
    explicit PageManagementDraft(labelqt::core::Project project);
    const labelqt::core::Project& project() const;
    const QVector<int>& order() const;
    const ProjectImageRenamePlan& pending() const;
    const State& state() const;
    void restore(State state);
    void setOrder(QVector<int> order);
    ProjectImageRenamePlan preview(const ProjectImageRenamePlan& incoming) const;
    bool queue(const ProjectImageRenamePlan& incoming);
    void clearPending(const QVector<int>& indexes);
    ProjectImageRenameItem pendingItem(int index) const;

private:
    void pruneRemoved();
    labelqt::core::Project m_project;
    State m_state;
};

} // namespace labelqt::services
