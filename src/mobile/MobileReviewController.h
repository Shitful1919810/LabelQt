#pragma once

#include "services/proofread/ReviewRoundEditService.h"

#include <QFutureWatcher>
#include <QVariantList>
#include <functional>

class MobileReviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString roundState READ roundState NOTIFY changed)
    Q_PROPERTY(QVariantList changes READ changes NOTIFY changed)
    Q_PROPERTY(QVariantList notes READ notes NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    // Both owners must outlive this adapter; invoke and refresh on their owner thread.
    MobileReviewController(labelqt::services::ProjectController& controller, labelqt::core::UndoStack& undo,
                           QObject* parent = nullptr);
    ~MobileReviewController() override;
    QString roundState() const
    {
        return m_roundState;
    }
    QVariantList changes() const
    {
        return m_changes;
    }
    QVariantList notes() const
    {
        return m_notes;
    }
    bool busy() const
    {
        return m_busy;
    }
    QString error() const
    {
        return m_error;
    }

    // The parent also calls refresh after external edits, including undo/redo.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearError()
    {
        setError({});
    }
    Q_INVOKABLE void startRound(bool replaceCompleted = false);
    Q_INVOKABLE void completeRound();
    Q_INVOKABLE bool addNote(QString pageId, QString labelId, QString text);
    Q_INVOKABLE void resolveNote(QString noteId, bool resolved);
    Q_INVOKABLE void calculateChanges();

signals:
    void changed();
    void edited();
    void errorChanged();
    void busyChanged();

private:
    using Round = std::optional<labelqt::services::ReviewRound>;
    using Result = std::expected<QVariantList, QString>;
    bool edit(const QString& description, const std::function<QString(Round&)>& mutate);
    void setError(QString error);
    labelqt::services::ProjectController& m_controller;
    labelqt::core::UndoStack& m_undo;
    QFutureWatcher<Result> m_watcher;
    labelqt::services::ReviewEditToken m_version{};
    QUuid m_documentSession;
    QString m_roundState{QStringLiteral("none")};
    QVariantList m_changes, m_notes;
    QString m_error;
    bool m_busy{false};
};
