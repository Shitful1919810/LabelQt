#include "services/label/LabelEditController.h"

#include "core/ProjectIdentityService.h"
#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace labelqt::services {

std::expected<bool, QString> LabelEditController::replaceTexts(const QByteArray& revision,
                                                               QVector<LabelTextReplacement> edits,
                                                               const QString& commandText)
{
    const auto stale = []() -> std::expected<bool, QString> {
        return std::unexpected(QCoreApplication::translate("LabelEditController",
                                                           "The project changed. Refresh the replacement preview."));
    };
    if (revision != ProjectTextSearchService::revision(m_project))
        return stale();
    QSet<QPair<int, int>> seen;
    for (const auto& edit : edits) {
        const auto* image = imageAt(edit.imageIndex);
        if (!image || edit.labelIndex < 0 || edit.labelIndex >= image->labels.size() ||
            image->labels.at(edit.labelIndex).isDeleted() || image->labels.at(edit.labelIndex).text() != edit.before ||
            seen.contains({edit.imageIndex, edit.labelIndex}))
            return stale();
        seen.insert({edit.imageIndex, edit.labelIndex});
    }
    QVector<LabelChange> changes;
    for (const auto& edit : edits) {
        const auto& page = m_project.images().at(edit.imageIndex);
        LabelChange change;
        change.pageId = page.id;
        change.labelId = page.labels.at(edit.labelIndex).id();
        change.text = edit.after;
        changes.append(std::move(change));
    }
    const auto committed = commitChanges(std::move(changes), commandText);
    if (!committed)
        return stale();
    return !committed->changedLabels.isEmpty();
}

std::expected<LabelCommitReceipt, LabelCommitError> LabelEditController::commitChanges(QVector<LabelChange> changes,
                                                                                       const QString& commandText)
{
    if (QThread::currentThread() != m_ownerThread)
        return std::unexpected(LabelCommitError::WrongThread);
    if (!labelqt::core::ProjectIdentityService::valid(m_project))
        return std::unexpected(LabelCommitError::InvalidIdentity);
    QVector<CommittedLabelChange> committed;
    QSet<QUuid> seen;
    const auto& pages = std::as_const(m_project).images();
    for (auto change : changes) {
        const auto page = std::ranges::find(pages, change.pageId, &labelqt::core::ImageEntry::id);
        if (page == pages.end())
            return std::unexpected(LabelCommitError::InvalidIdentity);
        if (change.groupId && m_project.groupName(*change.groupId).isEmpty())
            return std::unexpected(LabelCommitError::InvalidChange);
        if (change.position &&
            (!std::isfinite(change.position->x()) || !std::isfinite(change.position->y()) || change.position->x() < 0 ||
             change.position->x() > 1 || change.position->y() < 0 || change.position->y() > 1))
            return std::unexpected(LabelCommitError::InvalidChange);
        if (change.labelId.isNull()) {
            if (!change.text || !change.groupId || !change.position || change.deleted.value_or(false))
                return std::unexpected(LabelCommitError::InvalidChange);
            labelqt::core::Label label(*change.text, m_project.groupName(*change.groupId), *change.position);
            change.labelId = label.id();
            committed.append({change, {}, std::move(label)});
            continue;
        }
        const auto label = std::ranges::find(page->labels, change.labelId, &labelqt::core::Label::id);
        if (label == page->labels.end() || label->isDeleted() || seen.contains(change.labelId))
            return std::unexpected(LabelCommitError::InvalidChange);
        seen.insert(change.labelId);
        LabelChange reverse;
        reverse.pageId = change.pageId;
        reverse.labelId = change.labelId;
        if (change.text && *change.text != label->text())
            reverse.text = label->text();
        else
            change.text.reset();
        if (change.groupId && *change.groupId != m_project.groupId(label->group()))
            reverse.groupId = m_project.groupId(label->group());
        else
            change.groupId.reset();
        if (change.position && *change.position != label->position())
            reverse.position = label->position();
        else
            change.position.reset();
        if (change.deleted && *change.deleted != label->isDeleted())
            reverse.deleted = label->isDeleted();
        else
            change.deleted.reset();
        if (change.text || change.groupId || change.position || change.deleted)
            committed.append({std::move(change), std::move(reverse), {}});
    }
    LabelCommitReceipt receipt;
    for (const auto& item : committed) {
        receipt.changedLabels.append(item.forward.labelId);
        if (item.addition)
            receipt.createdLabels.append(item.forward.labelId);
    }
    if (committed.isEmpty())
        return receipt;
    // Mutation and history registration complete before notifying observers or entering any event loop.
    if (!applyCommittedChanges(committed, true))
        return std::unexpected(LabelCommitError::InvalidChange);
    m_undoStack.push(
        commandText,
        [this, committed] {
            if (!applyCommittedChanges(committed, false))
                return;
            markDirty();
            if (m_projectChanged)
                m_projectChanged();
        },
        [this, committed] {
            if (!applyCommittedChanges(committed, true))
                return;
            markDirty();
            if (m_projectChanged)
                m_projectChanged();
        });
    markDirty();
    if (m_projectChanged)
        m_projectChanged();
    return receipt;
}

bool LabelEditController::applyCommittedChanges(const QVector<CommittedLabelChange>& changes, bool forward)
{
    QVector<QPair<int, int>> locations;
    const auto& pages = std::as_const(m_project).images();
    for (const auto& item : changes) {
        const auto page = std::ranges::find(pages, item.forward.pageId, &labelqt::core::ImageEntry::id);
        if (page == pages.end())
            return false;
        const auto label = std::ranges::find(page->labels, item.forward.labelId, &labelqt::core::Label::id);
        const bool found = label != page->labels.end();
        const bool shouldExist = !item.addition || !forward;
        if (found != shouldExist)
            return false;
        const auto& change = forward ? item.forward : item.reverse;
        if (change.groupId && m_project.groupName(*change.groupId).isEmpty())
            return false;
        locations.append(
            {static_cast<int>(page - pages.begin()), found ? static_cast<int>(label - page->labels.begin()) : -1});
    }
    // Resolve the entire replay first. No partially applied undo if a host invariant was violated.
    for (qsizetype i = 0; i < changes.size(); ++i) {
        const auto& item = changes.at(i);
        auto& page = m_project.images()[locations.at(i).first];
        if (item.addition) {
            if (forward)
                page.labels.append(*item.addition);
            else
                page.labels.removeIf([&item](const auto& label) { return label.id() == item.addition->id(); });
            continue;
        }
        const auto& change = forward ? item.forward : item.reverse;
        auto& label = page.labels[locations.at(i).second];
        if (change.text)
            label.setText(*change.text);
        if (change.groupId)
            label.setGroup(m_project.groupName(*change.groupId));
        if (change.position)
            label.setPosition(*change.position);
        if (change.deleted)
            label.setDeleted(*change.deleted);
    }
    return true;
}

} // namespace labelqt::services
