#include "ui/proofread/ProofreadChangeTableModel.h"

#include <limits>
#include <ranges>
#include <utility>

namespace {
QString pageName(const labelqt::services::ReviewChange& change)
{
    return change.imageName.isEmpty() ? change.baselineImageName : change.imageName;
}
} // namespace

ProofreadChangeTableModel::ProofreadChangeTableModel(QVector<labelqt::services::ReviewChange> changes,
                                                     ProofreadChangeTableTexts texts, QObject* parent)
    : QAbstractTableModel(parent), m_changes(std::move(changes)), m_texts(std::move(texts))
{
}

int ProofreadChangeTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_changes.size());
}

int ProofreadChangeTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ProofreadChangeTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_changes.size()) {
        return {};
    }

    const auto& change = m_changes.at(index.row());
    const auto annotation = m_annotations.value(index.row());
    if (role == ReviewStatusRole)
        return static_cast<int>(annotation.status);
    if (role == Qt::ToolTipRole && change.manuallyMatched && index.column() == KindColumn)
        return m_manualPairText;
    const int sortNumber = change.labelIndex < 0 ? std::numeric_limits<int>::max() : change.labelIndex;
    if (role == ChangeIndexRole)
        return index.row();
    if (role == SortNumberRole)
        return sortNumber;

    QString text;
    switch (index.column()) {
    case PageColumn:
        text = pageName(change);
        break;
    case NumberColumn:
        text = change.labelIndex < 0 ? QStringLiteral("-") : QString::number(change.labelIndex + 1);
        break;
    case KindColumn:
        text = kindText(change.kind);
        break;
    case SummaryColumn:
        text = summaryText(change);
        break;
    case ReviewColumn:
        text = m_statusNames.value(static_cast<int>(annotation.status));
        if (role == Qt::ToolTipRole && !annotation.note.isEmpty())
            text += QLatin1Char('\n') + annotation.note;
        break;
    default:
        return {};
    }
    if (role == SortTextRole)
        return index.column() == NumberColumn ? QString() : text;
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)
        return text;
    return {};
}

QVariant ProofreadChangeTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= m_texts.headers.size()) {
        return {};
    }
    return m_texts.headers.at(section);
}

const labelqt::services::ReviewChange* ProofreadChangeTableModel::changeAt(int row) const noexcept
{
    return row >= 0 && row < m_changes.size() ? &m_changes.at(row) : nullptr;
}

const QVector<labelqt::services::ReviewChange>& ProofreadChangeTableModel::changes() const noexcept
{
    return m_changes;
}

void ProofreadChangeTableModel::setChanges(QVector<labelqt::services::ReviewChange> changes)
{
    beginResetModel();
    m_changes = std::move(changes);
    m_annotations.clear();
    endResetModel();
}

void ProofreadChangeTableModel::setReviewAnnotations(QVector<labelqt::services::ReviewAnnotation> annotations,
                                                     QStringList statusNames, QString manualPairText)
{
    m_annotations = std::move(annotations);
    m_statusNames = std::move(statusNames);
    m_manualPairText = std::move(manualPairText);
    if (!m_changes.isEmpty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

QString ProofreadChangeTableModel::kindText(labelqt::services::ReviewChangeKind kind) const
{
    switch (kind) {
    case labelqt::services::ReviewChangeKind::Comment:
        return m_texts.comment;
    case labelqt::services::ReviewChangeKind::Added:
        return m_texts.added;
    case labelqt::services::ReviewChangeKind::Deleted:
        return m_texts.deleted;
    case labelqt::services::ReviewChangeKind::Modified:
        return m_texts.modified;
    }
    return m_texts.modified;
}

QString ProofreadChangeTableModel::summaryText(const labelqt::services::ReviewChange& change) const
{
    if (change.kind != labelqt::services::ReviewChangeKind::Modified)
        return kindText(change.kind);
    QStringList parts;
    if (change.manuallyMatched)
        parts.append(m_manualPairText);
    for (const auto facet : labelqt::services::ReviewChangeClassifier::facets(change)) {
        parts.append(m_texts.facets.value(facet));
    }
    return parts.isEmpty() ? m_texts.modified : parts.join(QStringLiteral(", "));
}

ProofreadChangeFilterModel::ProofreadChangeFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void ProofreadChangeFilterModel::setFilters(QSet<QString> pages, QSet<QString> kinds, QSet<QString> facets,
                                            bool allFacetsSelected)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
    m_pages = std::move(pages);
    m_kinds = std::move(kinds);
    m_facets = std::move(facets);
    m_allFacetsSelected = allFacetsSelected;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    endFilterChange();
#else
    invalidateFilter();
#endif
}

bool ProofreadChangeFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    Q_UNUSED(sourceParent);
    const auto* model = qobject_cast<const ProofreadChangeTableModel*>(sourceModel());
    if (model == nullptr)
        return false;
    if (m_reviewStatus >= 0 &&
        model->index(sourceRow, 0).data(ProofreadChangeTableModel::ReviewStatusRole).toInt() != m_reviewStatus)
        return false;
    const auto* change = model->changeAt(sourceRow);
    if (change == nullptr || !m_pages.contains(pageName(*change)) ||
        !m_kinds.contains(labelqt::services::ReviewChangeClassifier::kindKey(change->kind))) {
        return false;
    }
    if (m_allFacetsSelected)
        return true;
    return std::ranges::any_of(labelqt::services::ReviewChangeClassifier::facets(*change), [this](auto facet) {
        return m_facets.contains(labelqt::services::ReviewChangeClassifier::facetKey(facet));
    });
}

void ProofreadChangeFilterModel::setReviewStatusFilter(int status)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
    m_reviewStatus = status;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    endFilterChange();
#else
    invalidateFilter();
#endif
}

bool ProofreadChangeFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const QString lhs = left.data(ProofreadChangeTableModel::SortTextRole).toString();
    const QString rhs = right.data(ProofreadChangeTableModel::SortTextRole).toString();
    const int comparison = QString::localeAwareCompare(lhs, rhs);
    return comparison == 0 ? left.data(ProofreadChangeTableModel::SortNumberRole).toInt() <
                                 right.data(ProofreadChangeTableModel::SortNumberRole).toInt()
                           : comparison < 0;
}
