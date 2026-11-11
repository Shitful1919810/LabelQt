#include "LabelVisibility.h"

namespace labelqt::services {

bool LabelVisibility::isVisible(const core::Label& label, const QSet<QString>& groups)
{
    return !label.isDeleted() && groups.contains(label.group());
}

int LabelVisibility::displayNumber(const QVector<core::Label>& labels, const QSet<QString>& groups, int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= labels.size() || !isVisible(labels.at(sourceIndex), groups))
        return 0;
    int number = 0;
    for (int i = 0; i <= sourceIndex; ++i)
        if (isVisible(labels.at(i), groups))
            ++number;
    return number;
}

QVector<int> LabelVisibility::sourceIndexes(const QVector<core::Label>& labels, const QSet<QString>& groups)
{
    QVector<int> indexes;
    indexes.reserve(labels.size());
    for (int index = 0; index < labels.size(); ++index)
        if (isVisible(labels.at(index), groups))
            indexes.append(index);
    return indexes;
}

} // namespace labelqt::services
