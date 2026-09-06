#include "ui/search/ProjectTextSearchModel.h"
#include <QCoreApplication>

ProjectTextSearchModel::ProjectTextSearchModel(QObject* parent) : QAbstractTableModel(parent) {}
void ProjectTextSearchModel::setResult(labelqt::services::ProjectTextSearchResult result)
{
    beginResetModel();
    m_result = std::move(result);
    endResetModel();
}
const labelqt::services::ProjectTextSearchResult& ProjectTextSearchModel::result() const
{
    return m_result;
}
int ProjectTextSearchModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_result.matches.size());
}
int ProjectTextSearchModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 3;
}
QVariant ProjectTextSearchModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid) || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
        return {};
    const auto& match = m_result.matches.at(index.row());
    if (index.column() == 0)
        return match.imageName;
    if (index.column() == 1)
        return match.labelIndex + 1;
    return match.text;
}
QVariant ProjectTextSearchModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    if (section == 0)
        return QCoreApplication::translate("ProjectTextSearchModel", "Page");
    if (section == 1)
        return QStringLiteral("#");
    if (section == 2)
        return QCoreApplication::translate("ProjectTextSearchModel", "Text");
    return {};
}
