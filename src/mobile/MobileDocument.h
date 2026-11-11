#pragma once

#include "mobile/MobileLabelModel.h"
#include "services/label/LabelEditController.h"
#include "services/project/ProjectController.h"
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace labelqt::mobile {

// Active-document presentation/command adapter; storage transfer and platform intents live elsewhere.
class MobileDocument final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool opened READ opened NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QStringList pageNames READ pageNames NOTIFY changed)
    Q_PROPERTY(int pageIndex READ pageIndex NOTIFY changed)
    Q_PROPERTY(QUrl imageSource READ imageSource NOTIFY changed)
    Q_PROPERTY(QString pageId READ pageId NOTIFY changed)
    Q_PROPERTY(QStringList groups READ groups NOTIFY changed)
    Q_PROPERTY(QStringList visibleGroups READ visibleGroups NOTIFY changed)
    Q_PROPERTY(QObject* labels READ labels CONSTANT)
    Q_PROPERTY(QString selectedLabel READ selectedLabel NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedGroup READ selectedGroup NOTIFY selectionChanged)
    Q_PROPERTY(QString textDraft READ textDraft NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MobileDocument(QObject* parent = nullptr);
    ~MobileDocument() override;
    bool openProject(const QString& path, const QString& title = {});
    // Used only after confirmed workspace removal; never writes the detached project back.
    void closeRemovedProject();
    void setTitle(QString title);
    void setGroupStyles(QVector<core::LabelGroupStyle> styles)
    {
        m_labels.setGroupStyles(std::move(styles));
    }
    services::ProjectController& controller()
    {
        return m_controller;
    }
    core::UndoStack& undoStack()
    {
        return m_undo;
    }
    bool opened() const
    {
        return !m_controller.project().filePath().isEmpty();
    }
    QString title() const;
    QStringList pageNames() const;
    int pageIndex() const
    {
        return m_pageIndex;
    }
    QUrl imageSource() const;
    QString pageId() const
    {
        return page() ? page()->id.toString(QUuid::WithoutBraces) : QString{};
    }
    QStringList groups() const
    {
        return m_controller.project().groups();
    }
    QStringList visibleGroups() const
    {
        QStringList visible;
        for (const auto& group : groups())
            if (m_visibleGroups.contains(group))
                visible.append(group);
        return visible;
    }
    QObject* labels()
    {
        return &m_labels;
    }
    QString selectedLabel() const
    {
        return m_selected.isNull() ? QString{} : m_selected.toString(QUuid::WithoutBraces);
    }
    QString selectedGroup() const;
    QString textDraft() const
    {
        return m_draft;
    }
    bool canUndo() const
    {
        return m_undo.canUndo();
    }
    bool canRedo() const
    {
        return m_undo.canRedo();
    }
    bool dirty() const
    {
        return m_controller.isDirty() || m_draft != m_editBaseline;
    }
    QString error() const
    {
        return m_error;
    }

    Q_INVOKABLE bool save();
    Q_INVOKABLE bool commitText();
    Q_INVOKABLE void setPage(int index);
    Q_INVOKABLE void selectLabel(const QString& key);
    Q_INVOKABLE void updateText(const QString& text);
    Q_INVOKABLE void addLabel(qreal x, qreal y);
    Q_INVOKABLE void moveLabel(const QString& key, qreal x, qreal y);
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void setGroup(const QString& group);
    Q_INVOKABLE void addGroup(const QString& group);
    Q_INVOKABLE void setVisibleGroups(const QStringList& groups);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearError();
    void refresh();

signals:
    void changed();
    void selectionChanged();
    void errorChanged();
    void edited();

private:
    int selectedIndex() const;
    const core::ImageEntry* page() const;
    void restoreEditor();
    void reportError(const QString& error);
    void didEdit();
    services::ProjectController m_controller;
    core::UndoStack m_undo;
    services::LabelEditController m_edits;
    MobileLabelModel m_labels;
    QTimer m_textCommit;
    QTimer m_autoSave;
    int m_pageIndex = -1;
    QUuid m_selected;
    QStringList m_visibleGroups;
    QString m_draft;
    QString m_editBaseline;
    QString m_error;
    QString m_title;
};
} // namespace labelqt::mobile
