#pragma once
#include "services/label/ProjectTextSearchService.h"
#include <QAbstractTableModel>

class ProjectTextSearchModel final : public QAbstractTableModel {
public:
    explicit ProjectTextSearchModel(QObject* parent = nullptr);
    void setResult(labelqt::services::ProjectTextSearchResult result);
    const labelqt::services::ProjectTextSearchResult& result() const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    labelqt::services::ProjectTextSearchResult m_result;
};
