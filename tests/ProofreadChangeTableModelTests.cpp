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
    void reviewStatusesExposeTextAndRemainFilterable()
    {
        using labelqt::services::ReviewStatus;
        ProofreadChangeTableModel model({change("page.png", 0, labelqt::services::ReviewChangeKind::Modified),
                                         change("page.png", 1, labelqt::services::ReviewChangeKind::Modified),
                                         change("page.png", 2, labelqt::services::ReviewChangeKind::Modified)},
                                        {});
        QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QStringList names{"Unseen", "Reviewed", "Needs confirmation"};
        model.setReviewAnnotations({{ReviewStatus::Unseen, {}},
                                    {ReviewStatus::Reviewed, "legacy note"},
                                    {ReviewStatus::NeedsConfirmation, {}}},
                                   names, "Manual pair");
        for (int row = 0; row < 3; ++row) {
            const auto index = model.index(row, ProofreadChangeTableModel::ReviewColumn);
            QCOMPARE(index.data().toString(), names[row]);
            QCOMPARE(index.data(Qt::AccessibleTextRole).toString(), names[row]);
            QCOMPARE(index.data(ProofreadChangeTableModel::ReviewStatusRole).toInt(), row);
        }
        QVERIFY(model.index(1, ProofreadChangeTableModel::ReviewColumn)
                    .data(Qt::ToolTipRole)
                    .toString()
                    .contains("legacy note"));
        ProofreadChangeFilterModel filter;
        filter.setSourceModel(&model);
        QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
        filter.setFilters({"page.png"}, {"modified"}, {}, true);
        filter.setReviewStatusFilter(static_cast<int>(ReviewStatus::Reviewed));
        QCOMPARE(filter.rowCount(), 1);
        model.setReviewAnnotations({}, names, "Manual pair");
        QCOMPARE(filter.rowCount(), 0);
    }

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
