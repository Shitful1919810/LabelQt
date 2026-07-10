#include "ui/proofread/ProofreadChangeTableModel.h"

#include <QAbstractItemModelTester>
#include <QtTest/QtTest>

#include <utility>

namespace {
labelqt::services::ReviewChange change(QString imageName, int labelIndex, labelqt::services::ReviewChangeKind kind)
{
    labelqt::services::ReviewChange result;
    result.imageName = std::move(imageName);
    result.labelIndex = labelIndex;
    result.kind = kind;
    return result;
}
} // namespace

class ProofreadChangeTableModelTests final : public QObject {
    Q_OBJECT

private slots:
    void modelAndCombinedFiltersRemainConsistent()
    {
        QVector<labelqt::services::ReviewChange> changes{
            change(QStringLiteral("002.png"), 4, labelqt::services::ReviewChangeKind::Added),
            change(QStringLiteral("001.png"), 1, labelqt::services::ReviewChangeKind::Modified),
            change(QStringLiteral("001.png"), 0, labelqt::services::ReviewChangeKind::Deleted),
        };
        changes[1].textChanged = true;
        changes[1].baseline.text = QStringLiteral("old");
        changes[1].current.text = QStringLiteral("new");

        ProofreadChangeTableModel model(
            changes,
            {{QStringLiteral("Page"), QStringLiteral("#"), QStringLiteral("Change"), QStringLiteral("Summary")},
             QStringLiteral("Added"),
             QStringLiteral("Deleted"),
             QStringLiteral("Modified"),
             {{labelqt::services::ReviewChangeFacet::Text, QStringLiteral("text")}}});
        QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        ProofreadChangeFilterModel filter;
        filter.setSourceModel(&model);
        QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);

        filter.setFilters({QStringLiteral("001.png")}, {QStringLiteral("modified")}, {QStringLiteral("text")}, false);
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.index(0, ProofreadChangeTableModel::NumberColumn).data().toString(), QStringLiteral("2"));
        QCOMPARE(filter.index(0, 0).data(ProofreadChangeTableModel::ChangeIndexRole).toInt(), 1);
    }

    void sortingUsesLabelNumberAsStableTieBreaker()
    {
        QVector<labelqt::services::ReviewChange> changes{
            change(QStringLiteral("001.png"), 9, labelqt::services::ReviewChangeKind::Modified),
            change(QStringLiteral("001.png"), 1, labelqt::services::ReviewChangeKind::Modified),
        };
        ProofreadChangeTableModel model(changes, {{QStringLiteral("Page"), QStringLiteral("#"),
                                                   QStringLiteral("Change"), QStringLiteral("Summary")},
                                                  QStringLiteral("Added"),
                                                  QStringLiteral("Deleted"),
                                                  QStringLiteral("Modified"),
                                                  {}});
        ProofreadChangeFilterModel filter;
        filter.setSourceModel(&model);
        filter.setFilters({QStringLiteral("001.png")}, {QStringLiteral("modified")}, {}, true);
        filter.sort(ProofreadChangeTableModel::PageColumn);

        QCOMPARE(filter.index(0, ProofreadChangeTableModel::NumberColumn).data().toString(), QStringLiteral("2"));
        QCOMPARE(filter.index(1, ProofreadChangeTableModel::NumberColumn).data().toString(), QStringLiteral("10"));
    }
};

QTEST_MAIN(ProofreadChangeTableModelTests)
#include "ProofreadChangeTableModelTests.moc"
