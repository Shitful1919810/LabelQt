#pragma once

#include "services/proofread/ReviewChangeClassifier.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVector>

struct ProofreadChangeTableTexts {
    QStringList headers;
    QString added;
    QString deleted;
    QString modified;
    QHash<labelqt::services::ReviewChangeFacet, QString> facets;
};

class ProofreadChangeTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { PageColumn, NumberColumn, KindColumn, SummaryColumn, ColumnCount };
    enum Role { ChangeIndexRole = Qt::UserRole + 1, SortTextRole, SortNumberRole };

    ProofreadChangeTableModel(QVector<labelqt::services::ReviewChange> changes, ProofreadChangeTableTexts texts,
                              QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    const labelqt::services::ReviewChange* changeAt(int row) const noexcept;
    const QVector<labelqt::services::ReviewChange>& changes() const noexcept;

private:
    QString kindText(labelqt::services::ReviewChangeKind kind) const;
    QString summaryText(const labelqt::services::ReviewChange& change) const;

    QVector<labelqt::services::ReviewChange> m_changes;
    ProofreadChangeTableTexts m_texts;
};

class ProofreadChangeFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ProofreadChangeFilterModel(QObject* parent = nullptr);

    void setFilters(QSet<QString> pages, QSet<QString> kinds, QSet<QString> facets, bool allFacetsSelected);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QSet<QString> m_pages;
    QSet<QString> m_kinds;
    QSet<QString> m_facets;
    bool m_allFacetsSelected{true};
};
