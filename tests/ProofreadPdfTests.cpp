#include "services/proofread/ProofreadReportContent.h"
#include "services/proofread/ReviewRoundService.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace labelqt::core;
using namespace labelqt::services;

namespace {
QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
} // namespace

class ProofreadPdfTests : public QObject {
    Q_OBJECT
private slots:
    void portraitReportPreviewIsBounded()
    {
        QTemporaryDir directory;
        QImage tall(120, 2400, QImage::Format_RGB32);
        tall.fill(Qt::white);
        const auto path = directory.filePath("tall.png");
        QVERIFY(tall.save(path));
        ProofreadReportOptions options;
        options.maxImageWidth = 480;
        const auto preview = report::loadImage(path, options);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview.size(), QSize(24, 480));
        QVERIFY(report::loadImage({}, options).isNull());
        options.maxImageWidth = 0;
        const auto minimum = report::loadImage(path, options);
        QVERIFY(minimum.isNull() || (minimum.width() <= 1 && minimum.height() <= 1));
    }

    void paginatedReportAndCancellation()
    {
        QTemporaryDir directory;
        Project before;
        before.setFilePath(directory.filePath("project.txt"));
        before.setGroups({"dialogue"});
        QImage image(640, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.drawRect(20, 20, 600, 760);
        painter.drawLine(20, 400, 620, 400);
        painter.end();
        QVERIFY(image.save(directory.filePath("page.png")));
        before.images() = {
            {"first.png",
             directory.filePath("page.png"),
             {Label("Original wording", "dialogue", {0, 0}), Label("Unchanged label", "dialogue", {1, 1})}},
            {"second.png", directory.filePath("missing.png"), {Label("second page", "dialogue", {.4, .4})}}};
        auto after = before;
        const QString longText =
            QStringLiteral("Long translated paragraph. 中文校对与换行。\n").repeated(180) + "END_OF_LONG_TEXT";
        after.images()[0].labels[0].setText(longText);
        after.images()[1].labels[0].setText("SECOND_PAGE_CHANGED");
        ReviewRound round;
        round.before = before;
        ProofreadNote note;
        note.pageId = before.images()[0].id;
        note.pageName = "first.png";
        note.text = "PAGE_NOTE <script>not executable</script>";
        round.notes.append(note);
        const auto report = ReviewRoundService::present(round, after);
        const auto texts = ProofreadReportService::defaultTexts("Original draft", "Current draft");
        ProofreadReportOptions options;
        options.includeReview = true;
        const auto path = directory.filePath("report.pdf");
        const auto result = ProofreadReportService::savePdfReport(path, report.changes, report.before, report.after,
                                                                  texts, "ALL_PAGES_FILTER", options);
        QVERIFY2(result.has_value(), qPrintable(result ? QString{} : result.error()));
        const auto bytes = read(path);
        QVERIFY(bytes.startsWith("%PDF-"));
        QVERIFY(bytes.trimmed().endsWith("%%EOF"));
        const auto groups = report::groupedChanges(report.changes);
        QCOMPARE(groups.size(), 2);
        QCOMPARE(report::markersForChanges(groups.first(), report.changes, true).size(), 1);
        const auto html = ProofreadReportService::htmlReport(report.changes, report.before, report.after, texts,
                                                             "ALL_PAGES_FILTER", options);
        QVERIFY(html.contains("PAGE_NOTE &lt;script&gt;"));
        const auto textTool = QStandardPaths::findExecutable("pdftotext");
        if (!textTool.isEmpty()) {
            QProcess process;
            process.start(textTool, {path, "-"});
            QVERIFY(process.waitForFinished(10000));
            QCOMPARE(process.exitCode(), 0);
            const auto text = process.readAllStandardOutput();
            for (const auto* expected : {"END_OF_LONG_TEXT", "SECOND_PAGE_CHANGED", "PAGE_NOTE", "ALL_PAGES_FILTER",
                                         "Original draft", "Current draft", "Unseen"})
                QVERIFY2(text.contains(expected), expected);
            QVERIFY(text.count('\f') > 2);
        }
        const auto artifacts = qEnvironmentVariable("LABELQT_REPORT_TEST_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(QFile::copy(path, QDir(artifacts).filePath("report.pdf")));
            QVERIFY(ProofreadReportService::saveHtmlReport(QDir(artifacts).filePath("report.html"), report.changes,
                                                           report.before, report.after, texts, "ALL_PAGES_FILTER",
                                                           options));
        }
        int checks = 0;
        const auto canceled = ProofreadReportService::savePdfReport(path, report.changes, report.before, report.after,
                                                                    texts, {}, options, [&] { return ++checks > 4; });
        QVERIFY(!canceled);
        QCOMPARE(read(path), bytes);
        QVERIFY(!ProofreadReportService::savePdfReport(directory.path(), report.changes, before, after, texts));
        QVERIFY(!ProofreadReportService::savePdfReport(path, {}, before, after, texts));
        QCOMPARE(read(path), bytes);
        auto filtered = report.changes;
        filtered.removeIf([&](const auto& change) { return change.pageId != before.images()[0].id; });
        QVERIFY(ProofreadReportService::savePdfReport(path, filtered, before, after, texts));
        if (!textTool.isEmpty()) {
            QProcess process;
            process.start(textTool, {path, "-"});
            QVERIFY(process.waitForFinished(10000));
            QVERIFY(!process.readAllStandardOutput().contains("SECOND_PAGE_CHANGED"));
        }
    }
};
QTEST_MAIN(ProofreadPdfTests)
#include "ProofreadPdfTests.moc"
