#pragma once

#include "mobile/MobileDocument.h"
#include "services/label/ProjectTextSearchService.h"
#include <QFutureWatcher>
#include <QVariantList>

namespace labelqt::mobile {
// Bounded asynchronous search presentation. Matching remains shared with desktop.
class MobileSearchController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY changed)
public:
    explicit MobileSearchController(MobileDocument& document, QObject* parent = nullptr);
    ~MobileSearchController() override;
    QVariantList results() const
    {
        return m_results;
    }
    bool busy() const
    {
        return m_running || m_timer.isActive() || m_pending;
    }
    bool truncated() const
    {
        return m_truncated;
    }
    QString error() const
    {
        return m_error;
    }
    int current() const
    {
        return m_current;
    }
    Q_INVOKABLE void configure(QString text, bool matchCase, int scope, int first, int last, bool allGroups,
                               QStringList groups);
    Q_INVOKABLE bool navigate(int row);

private:
    struct Result {
        QVariantList rows;
        bool truncated{false};
        QString error;
    };
    void observeDocument();
    void invalidate();
    void start();
    MobileDocument& m_document;
    services::ProjectTextQuery m_query;
    QTimer m_timer;
    QFutureWatcher<Result> m_worker;
    QVariantList m_results;
    QUuid m_session;
    quint64 m_revision{0}, m_generation{0};
    int m_scope{0}, m_page{-1}, m_current{-1};
    bool m_running{false}, m_pending{false}, m_truncated{false};
    QString m_error;
signals:
    void changed();
};
} // namespace labelqt::mobile
