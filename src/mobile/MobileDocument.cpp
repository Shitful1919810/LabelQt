#include "MobileDocument.h"
#include "core/LabelPlusDocument.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace labelqt::mobile {
namespace {
services::LabelEditCommandTexts commandTexts()
{
    // Keep command names in the existing translation context on both frontends.
    services::LabelEditCommandTexts texts;
    texts.addLabel = QCoreApplication::translate("MainWindow", "Add label");
    texts.editLabelText = QCoreApplication::translate("MainWindow", "Edit label text");
    texts.changeLabelGroup = QCoreApplication::translate("MainWindow", "Change label group");
    texts.moveLabel = QCoreApplication::translate("MainWindow", "Move label");
    texts.deleteLabels = QCoreApplication::translate("MainWindow", "Delete labels");
    texts.reorderLabels = QCoreApplication::translate("MainWindow", "Reorder labels");
    texts.pasteLabels = QCoreApplication::translate("MainWindow", "Paste labels");
    texts.addGroup = QCoreApplication::translate("MainWindow", "Add group");
    texts.removeGroup = QCoreApplication::translate("MainWindow", "Remove group");
    return texts;
}
} // namespace

MobileDocument::MobileDocument(QObject* parent) : QObject(parent), m_edits(m_controller, m_undo, commandTexts())
{
    m_textCommit.setSingleShot(true);
    m_textCommit.setInterval(750);
    m_autoSave.setSingleShot(true);
    m_autoSave.setInterval(1000);
    connect(&m_textCommit, &QTimer::timeout, this, &MobileDocument::commitText);
    connect(&m_autoSave, &QTimer::timeout, this, &MobileDocument::save);
    m_undo.setChangedCallback([this] { emit changed(); });
    m_edits.setCallbacks(
        [this](int pageIndex, int labelIndex) {
            if (pageIndex == m_pageIndex && page() && labelIndex >= 0 && labelIndex < page()->labels.size())
                m_selected = page()->labels.at(labelIndex).id();
        },
        {},
        [this](int pageIndex) {
            if (pageIndex == m_pageIndex)
                m_selected = {};
        },
        [this] { refresh(); }, [this] { didEdit(); });
}

MobileDocument::~MobileDocument()
{
    m_textCommit.stop();
    m_autoSave.stop();
    m_undo.setChangedCallback({});
    m_edits.setCallbacks({}, {}, {}, {}, {});
}

QString MobileDocument::title() const
{
    return m_title.isEmpty() ? QFileInfo(m_controller.project().filePath()).completeBaseName() : m_title;
}

void MobileDocument::setTitle(QString title)
{
    m_title = std::move(title);
    emit changed();
}

void MobileDocument::closeRemovedProject()
{
    m_textCommit.stop();
    m_autoSave.stop();
    m_undo.clear();
    m_controller.setProject({});
    m_pageIndex = -1;
    m_selected = {};
    m_visibleGroups.clear();
    m_title.clear();
    m_draft.clear();
    m_editBaseline.clear();
    clearError();
    restoreEditor();
    refresh();
    emit edited();
}

QStringList MobileDocument::pageNames() const
{
    QStringList names;
    for (const auto& image : m_controller.project().images())
        names.append(image.name);
    return names;
}

const core::ImageEntry* MobileDocument::page() const
{
    const auto& images = m_controller.project().images();
    return m_pageIndex >= 0 && m_pageIndex < images.size() ? &images.at(m_pageIndex) : nullptr;
}

int MobileDocument::selectedIndex() const
{
    if (!page() || m_selected.isNull())
        return -1;
    const auto& labels = page()->labels;
    for (int i = 0; i < labels.size(); ++i)
        if (labels.at(i).id() == m_selected && !labels.at(i).isDeleted())
            return i;
    return -1;
}

QString MobileDocument::selectedGroup() const
{
    const int index = selectedIndex();
    return index < 0 ? QString{} : page()->labels.at(index).group();
}

QUrl MobileDocument::imageSource() const
{
    return page() && !page()->path.isEmpty() ? QUrl::fromLocalFile(page()->path) : QUrl{};
}

void MobileDocument::reportError(const QString& error)
{
    m_error = error;
    emit errorChanged();
}

void MobileDocument::clearError()
{
    reportError({});
}

bool MobileDocument::openProject(QString path, QString title)
{
    // Saving emits callbacks that may replace the workspace model owning the caller's strings.
    if (opened() && !save())
        return false;
    try {
        m_controller.loadFromFile(path);
        m_title = std::move(title);
        m_undo.clear();
        m_pageIndex = m_controller.project().images().isEmpty() ? -1 : 0;
        m_selected = {};
        m_visibleGroups = groups();
        m_draft.clear();
        m_editBaseline.clear();
        clearError();
        refresh();
        restoreEditor();
        emit edited();
        return true;
    }
    catch (const std::exception& e) {
        reportError(QString::fromUtf8(e.what()));
        return false;
    }
}

bool MobileDocument::commitText()
{
    m_textCommit.stop();
    if (m_draft == m_editBaseline)
        return true;
    const int index = selectedIndex();
    if (index < 0 || page()->labels.at(index).text() != m_editBaseline) {
        reportError(tr("The edited label changed. Your text has been kept in the editor."));
        return false;
    }
    const QString text = m_draft;
    m_edits.setLabelText(m_pageIndex, index, text);
    m_editBaseline = text;
    return true;
}

bool MobileDocument::save()
{
    m_autoSave.stop();
    if (!commitText())
        return false;
    if (!opened())
        return true;
    try {
        if (m_controller.isDirty())
            m_controller.save();
        clearError();
        emit changed();
        return true;
    }
    catch (const std::exception& e) {
        reportError(QString::fromUtf8(e.what()));
        emit changed();
        return false;
    }
}

void MobileDocument::restoreEditor()
{
    const int index = selectedIndex();
    m_editBaseline = index >= 0 ? page()->labels.at(index).text() : QString{};
    m_draft = m_editBaseline;
    emit selectionChanged();
}

void MobileDocument::refresh()
{
    if (selectedIndex() < 0 || !m_visibleGroups.contains(selectedGroup())) {
        m_selected = {};
        // Do not discard an unresolved editor draft during a model refresh.
        if (m_draft == m_editBaseline)
            restoreEditor();
    }
    m_labels.refresh(page() ? page()->labels : QVector<core::Label>{}, visibleGroups(), groups());
    emit changed();
}

void MobileDocument::didEdit()
{
    refresh();
    m_autoSave.start();
    emit edited();
}

void MobileDocument::setPage(int index)
{
    if (index < 0 || index >= m_controller.project().images().size() || index == m_pageIndex || !commitText())
        return;
    m_pageIndex = index;
    m_selected = {};
    restoreEditor();
    refresh();
}

void MobileDocument::selectLabel(const QString& key)
{
    if (!commitText())
        return;
    m_selected = QUuid(key);
    if (selectedIndex() < 0 || !m_visibleGroups.contains(selectedGroup()))
        m_selected = {};
    restoreEditor();
    refresh();
}

void MobileDocument::updateText(const QString& text)
{
    if (selectedIndex() < 0 || text == m_draft)
        return;
    m_draft = text;
    m_textCommit.start();
    emit changed();
}

void MobileDocument::addLabel(qreal x, qreal y)
{
    const auto visible = visibleGroups();
    if (!page() || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 || visible.isEmpty() ||
        !commitText())
        return;
    const auto result = m_edits.addLabel(m_pageIndex, core::Label({}, visible.first(), {x, y}));
    if (result.changed && result.selectedLabelIndex >= 0 && result.selectedLabelIndex < page()->labels.size())
        m_selected = page()->labels.at(result.selectedLabelIndex).id();
    restoreEditor();
    refresh();
}

void MobileDocument::moveLabel(const QString& key, qreal x, qreal y)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !commitText())
        return;
    // A delayed drag can only affect the still-selected stable identity.
    if (key != selectedLabel() || selectedIndex() < 0)
        return;
    m_edits.setLabelPosition(m_pageIndex, selectedIndex(), {std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
}

void MobileDocument::deleteSelected()
{
    if (!commitText() || selectedIndex() < 0)
        return;
    m_edits.deleteLabels(m_pageIndex, {selectedIndex()});
    m_selected = {};
    restoreEditor();
    refresh();
}

void MobileDocument::setGroup(const QString& group)
{
    if (!commitText() || selectedIndex() < 0 || !groups().contains(group))
        return;
    m_edits.setLabelGroup(m_pageIndex, selectedIndex(), group);
    restoreEditor();
    refresh();
}

bool MobileDocument::addGroup(const QString& group)
{
    if (!opened() || !commitText())
        return false;
    const auto name = group.trimmed();
    if (!core::LabelPlusDocument::isValidGroupName(name) || groups().contains(name)) {
        reportError(tr("Use a unique, nonempty group name without line breaks or the reserved name '-'."));
        return false;
    }
    if (!m_edits.addGroup(name).changed)
        return false;
    m_visibleGroups.append(name);
    clearError();
    refresh();
    return true;
}

void MobileDocument::setVisibleGroups(const QStringList& requested)
{
    if (!commitText())
        return;
    m_visibleGroups.clear();
    for (const auto& group : groups())
        if (requested.contains(group))
            m_visibleGroups.append(group);
    refresh();
}

void MobileDocument::undo()
{
    if (!commitText())
        return;
    m_undo.undo();
    restoreEditor();
    didEdit();
}

void MobileDocument::redo()
{
    if (!commitText())
        return;
    m_undo.redo();
    restoreEditor();
    didEdit();
}
} // namespace labelqt::mobile
