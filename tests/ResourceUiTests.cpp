#include "services/io/ImagePageCache.h"
#include "ui/automation/AutomationRunDialog.h"
#include "ui/canvas/ImageCanvas.h"
#include "ui/project/MergeCandidatePreview.h"
#include "ui/project/ProjectMergeDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QListWidget>
#include <QScrollBar>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace {
constexpr int maxLogCharacters = 262144;
constexpr int maxLogBlocks = 5000;
constexpr int maxLogLineCharacters = 4096;

bool captureWidget(QWidget& widget, const QString& name)
{
    const QString path = qEnvironmentVariable("LABELQT_TEST_SCREENSHOTS");
    if (path.isEmpty())
        return true;
    QCoreApplication::processEvents();
    return QDir().mkpath(path) && widget.grab().save(QDir(path).filePath(name + QStringLiteral(".png")));
}

QImage canvasImage(ImageCanvas* canvas)
{
    for (auto* item : canvas->scene()->items()) {
        if (auto* pixmap = qgraphicsitem_cast<QGraphicsPixmapItem*>(item))
            return pixmap->pixmap().toImage();
    }
    return {};
}

labelqt::core::ImageEntry imageEntry(const QString& path)
{
    labelqt::core::ImageEntry image;
    image.path = path;
    image.name = path;
    return image;
}

// Timeout and RAII release keep failed assertions from stranding pool threads.
struct DecodeGate {
    std::atomic_int started{0};
    std::atomic_int finished{0};
    QSemaphore permits;
};

struct ReleaseDecoders {
    std::shared_ptr<DecodeGate> gate;
    ~ReleaseDecoders()
    {
        gate->permits.release(100);
        QThreadPool::globalInstance()->waitForDone();
    }
};

labelqt::services::ImagePageCache::Loader gatedLoader(const std::shared_ptr<DecodeGate>& gate)
{
    return [gate](const QString& path, QSize size) {
        ++gate->started;
        gate->permits.tryAcquire(1, 5000);
        labelqt::services::ImagePageLoadResult result;
        result.path = path;
        result.requestedPreviewSize = size;
        result.sourceRevision = *labelqt::services::FileRevision::read(path);
        result.image = QImage(path);
        result.originalSize = result.image.size();
        ++gate->finished;
        return result;
    };
}
} // namespace

class ResourceUiTests final : public QObject {
    Q_OBJECT

private slots:
    void imageCompletionStopsAfterReceiverInvalidatesCache_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("clear") << false;
        QTest::newRow("destroy") << true;
    }

    void imageCompletionStopsAfterReceiverInvalidatesCache()
    {
        QFETCH(bool, destroy);
        using labelqt::services::ImagePageCache;
        QTemporaryDir directory;
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::white);
        const auto path = directory.filePath("page.png");
        QVERIFY(image.save(path));
        auto gate = std::make_shared<DecodeGate>();
        ReleaseDecoders release{gate};
        auto cache = std::make_unique<ImagePageCache>(gatedLoader(gate));
        int notifications = 0;
        connect(cache.get(), &ImagePageCache::imageLoaded, this, [&] {
            ++notifications;
            if (destroy)
                cache.reset();
            else
                cache->clear();
        });
        cache->requestImage(path, QSize(16, 16));
        cache->requestImage(path, QSize(16, 16));
        QTRY_COMPARE(gate->started.load(), 1);
        gate->permits.release();
        QTRY_COMPARE(notifications, 1);
        QCoreApplication::processEvents();
        QCOMPARE(notifications, 1);
        QCOMPARE(bool(cache), !destroy);
    }

    void imageLoadPolicies_data()
    {
        QTest::addColumn<QString>("extension");
        QTest::newRow("png") << QStringLiteral("png");
        QTest::newRow("jpeg") << QStringLiteral("jpg");
    }

    void imageLoadPolicies()
    {
        QFETCH(QString, extension);
        using labelqt::services::ImagePageCache;
        using labelqt::services::ImagePageLoadResult;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("source.") + extension);
        QImage image(120, 80, QImage::Format_RGBA64);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        constexpr QSize targetSize(24, 24);
        constexpr qint64 sourcePixels = 120 * 80;
        const auto loader = ImagePageCache::previewLoader(sourcePixels);
        const auto bounded = loader(path, targetSize);
        QVERIFY(bounded.isValid());
        QCOMPARE(bounded.originalSize, image.size());
        QCOMPARE(bounded.requestedPreviewSize, targetSize);
        QCOMPARE(bounded.image.size(), QSize(24, 16));
        QCOMPARE(bounded.image.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(bounded.path, path);
        QVERIFY(bounded.errorMessage.isEmpty());
        QCOMPARE(loader(path, QSize(1000, 1000)).image.size(), image.size());
        const auto rejected = ImagePageCache::previewLoader(sourcePixels - 1)(path, targetSize);
        QVERIFY(!rejected.isValid());
        QCOMPARE(rejected.originalSize, image.size());
        QVERIFY(rejected.sourceRevision.sameContent(bounded.sourceRevision));
        QVERIFY(!ImagePageCache::previewLoader(0)(path, targetSize).isValid());
        QVERIFY(!ImagePageCache::previewLoader(-1)(path, targetSize).isValid());
        QVERIFY(!loader(path, QSize()).isValid());
        QVERIFY(!loader(path, QSize(0, 24)).isValid());
        QVERIFY(!loader(path, QSize(24, -1)).isValid());

        ImagePageCache normal;
        ImagePageCache preview(ImagePageCache::previewLoader(32 * 1024 * 1024));
        QSignalSpy normalLoaded(&normal, &ImagePageCache::imageLoaded);
        QSignalSpy previewLoaded(&preview, &ImagePageCache::imageLoaded);
        normal.requestImage(path, targetSize);
        preview.requestImage(path, targetSize);
        QTRY_COMPARE(normalLoaded.count(), 1);
        QTRY_COMPARE(previewLoaded.count(), 1);
        const auto full = qvariant_cast<ImagePageLoadResult>(normalLoaded.at(0).at(1));
        const auto cachedPreview = qvariant_cast<ImagePageLoadResult>(previewLoaded.at(0).at(1));
        QVERIFY(full.isValid());
        QCOMPARE(full.image.size(), image.size());
        QCOMPARE(full.image, QImage(path));
        QCOMPARE(full.requestedPreviewSize, targetSize);
        QCOMPARE(full.originalSize, image.size());
        QVERIFY(full.sourceRevision.sameContent(bounded.sourceRevision));
        QCOMPARE(cachedPreview.image, bounded.image);
        QVERIFY(cachedPreview.sourceRevision.sameContent(full.sourceRevision));
        // The normal policy still ignores display target sizes, including cache hits.
        const auto fullHit = normal.cachedImage(path, QSize(1, 1));
        QVERIFY(fullHit.has_value());
        QCOMPARE(fullHit->image.size(), image.size());

        QImage replacement(160, 80, QImage::Format_RGB32);
        replacement.fill(Qt::blue);
        QVERIFY(replacement.save(path));
        QVERIFY(!normal.cachedImage(path, targetSize));
        QVERIFY(!preview.cachedImage(path, targetSize));
        normal.requestImage(path, targetSize);
        preview.requestImage(path, targetSize);
        QTRY_COMPARE(normalLoaded.count(), 2);
        QTRY_COMPARE(previewLoaded.count(), 2);
        const auto reloadedFull = qvariant_cast<ImagePageLoadResult>(normalLoaded.at(1).at(1));
        const auto reloadedPreview = qvariant_cast<ImagePageLoadResult>(previewLoaded.at(1).at(1));
        QCOMPARE(reloadedFull.image.size(), replacement.size());
        QCOMPARE(reloadedPreview.image.size(), QSize(24, 12));
        QCOMPARE(reloadedPreview.originalSize, replacement.size());
        QVERIFY(!reloadedFull.sourceRevision.sameContent(full.sourceRevision));
        QVERIFY(reloadedPreview.sourceRevision.sameContent(reloadedFull.sourceRevision));
    }

    void imageLoadPoliciesShareFailures()
    {
        using labelqt::services::ImagePageCache;
        using labelqt::services::ImagePageLoadResult;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString corruptPath = dir.filePath(QStringLiteral("corrupt.png"));
        QFile corrupt(corruptPath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        QVERIFY(corrupt.write("not an image") > 0);
        corrupt.close();
        ImagePageCache normal;
        ImagePageCache preview(ImagePageCache::previewLoader(32 * 1024 * 1024));
        QSignalSpy normalLoaded(&normal, &ImagePageCache::imageLoaded);
        QSignalSpy previewLoaded(&preview, &ImagePageCache::imageLoaded);
        normal.requestImage(corruptPath, QSize(24, 24));
        preview.requestImage(corruptPath, QSize(24, 24));
        QTRY_COMPARE(normalLoaded.count(), 1);
        QTRY_COMPARE(previewLoaded.count(), 1);
        const auto full = qvariant_cast<ImagePageLoadResult>(normalLoaded.at(0).at(1));
        const auto bounded = qvariant_cast<ImagePageLoadResult>(previewLoaded.at(0).at(1));
        QVERIFY(!full.isValid());
        QVERIFY(!bounded.isValid());
        QVERIFY(!full.errorMessage.isEmpty());
        QCOMPARE(bounded.errorMessage, full.errorMessage);
        QVERIFY(bounded.sourceRevision.sameContent(full.sourceRevision));
        const auto missing =
            ImagePageCache::previewLoader(100)(dir.filePath(QStringLiteral("missing.png")), QSize(5, 5));
        QVERIFY(!missing.isValid());
        QVERIFY(!missing.sourceRevision.exists);
    }

    void logsBoundCumulativeNoNewlineOutput()
    {
        AutomationRunDialog dialog(QStringLiteral("test"));
        auto* edit = dialog.findChild<QTextEdit*>();
        QVERIFY(edit);
        const QString chunk(8192, QLatin1Char('x'));
        for (int i = 0; i < 150; ++i) {
            if (i % 2)
                dialog.appendStandardError(chunk);
            else
                dialog.appendStandardOutput(chunk);
            QVERIFY(edit->document()->characterCount() - 1 <= maxLogCharacters);
            QVERIFY(edit->document()->blockCount() <= maxLogBlocks);
        }
        for (QTextBlock block = edit->document()->begin(); block.isValid(); block = block.next())
            QVERIFY(block.length() - 1 <= maxLogLineCharacters);
        dialog.appendStandardError(QStringLiteral("TAIL"));
        QVERIFY(edit->toPlainText().endsWith(QStringLiteral("TAIL")));
        QTextCursor tail(edit->document());
        tail.movePosition(QTextCursor::End);
        tail.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        QCOMPARE(tail.charFormat().foreground().color(), QColor(190, 64, 64));
        QVERIFY(!edit->document()->isUndoAvailable());
    }

    void logsBoundChunksAndLines()
    {
        AutomationRunDialog dialog(QStringLiteral("test"));
        auto* edit = dialog.findChild<QTextEdit*>();
        dialog.appendStandardOutput(QStringLiteral("DISCARDED") + QString(1000000, QLatin1Char('z')));
        const QString text = edit->toPlainText();
        QVERIFY(!text.contains(QStringLiteral("DISCARDED")));
        QCOMPARE(text.count(QLatin1Char('z')), 65536);
        QVERIFY(text.contains(QStringLiteral("truncated")));
        dialog.appendStandardOutput(QStringLiteral("line\n").repeated(20000));
        QVERIFY(edit->document()->blockCount() <= maxLogBlocks);
        QVERIFY(edit->document()->characterCount() - 1 <= maxLogCharacters);
        // Unicode line separators must not leave one giant QTextBlock either.
        for (int i = 0; i < 20; ++i)
            dialog.appendStandardOutput(QString(10000, QChar::LineSeparator));
        QVERIFY(edit->document()->blockCount() <= maxLogBlocks);
        for (QTextBlock block = edit->document()->begin(); block.isValid(); block = block.next())
            QVERIFY(block.length() - 1 <= maxLogLineCharacters);
    }

    void logsPreserveScrollAndSelection()
    {
        AutomationRunDialog dialog(QStringLiteral("test"));
        dialog.show();
        auto* edit = dialog.findChild<QTextEdit*>();
        for (int i = 0; i < 1000; ++i)
            dialog.appendStandardOutput(QStringLiteral("%1 ").arg(i) + QString(300, QLatin1Char('x')) +
                                        QLatin1Char('\n'));
        QCoreApplication::processEvents();
        auto* scroll = edit->verticalScrollBar();
        QCOMPARE(scroll->value(), scroll->maximum());
        scroll->setValue(scroll->maximum() / 2);
        edit->horizontalScrollBar()->setValue(40);
        QTextCursor anchor = edit->cursorForPosition(QPoint(5, 5));
        const QString blockText = anchor.block().text();
        QTextCursor selection = anchor;
        selection.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 10);
        edit->setTextCursor(selection);
        const QString selectedText = edit->textCursor().selectedText();
        const int top = edit->cursorRect(anchor).top();
        const int horizontalPosition = edit->horizontalScrollBar()->value();
        for (int i = 0; i < 20; ++i)
            dialog.appendStandardError(QString(300, QLatin1Char('y')) + QLatin1Char('\n'));
        QCOMPARE(anchor.block().text(), blockText);
        QCOMPARE(edit->cursorRect(anchor).top(), top);
        QCOMPARE(edit->textCursor().selectedText(), selectedText);
        QCOMPARE(edit->horizontalScrollBar()->value(), horizontalPosition);
        QVERIFY(scroll->value() < scroll->maximum());
        scroll->setValue(scroll->maximum());
        dialog.appendStandardOutput(QStringLiteral("tail\n"));
        QCOMPARE(scroll->value(), scroll->maximum());
        scroll->setValue(scroll->maximum() / 2);
        dialog.appendStandardOutput(QStringLiteral("replacement\n").repeated(5500));
        QVERIFY(scroll->value() < scroll->maximum());
    }

    void logsKeepSplitCrLfAndSurrogatePairs()
    {
        AutomationRunDialog dialog(QStringLiteral("test"));
        auto* edit = dialog.findChild<QTextEdit*>();
        dialog.appendStandardOutput(QStringLiteral("one\r\ntwo\r"));
        dialog.appendStandardOutput(QStringLiteral("\nthree"));
        QCOMPARE(edit->toPlainText(), QStringLiteral("one\ntwo\nthree"));
        dialog.appendStandardOutput(QLatin1Char('\n') + QString(4096, QLatin1Char('x')) + QLatin1Char('\n'));
        QVERIFY(!edit->toPlainText().endsWith(QStringLiteral("\n\n")));
        dialog.appendStandardOutput(QLatin1Char('\n') + QString(4095, QLatin1Char('x')));
        const QString supplementary = QString::fromUcs4(U"\U00020000");
        dialog.appendStandardOutput(supplementary.repeated(5000));
        const QString text = edit->toPlainText();
        QCOMPARE(text.count(supplementary), 5000);
        QVERIFY(QStringView(text).isValidUtf16());
        for (QTextBlock block = edit->document()->begin(); block.isValid(); block = block.next())
            QVERIFY(block.length() - 1 <= maxLogLineCharacters);
    }

    void mergeOnlyLoadsCurrentPairAndRetainsChoices()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString redPath = dir.filePath(QStringLiteral("red.png"));
        const QString bluePath = dir.filePath(QStringLiteral("blue.png"));
        QImage image(80, 100, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(redPath));
        image.fill(Qt::blue);
        QVERIFY(image.save(bluePath));
        labelqt::services::ProjectMergePlan plan;
        plan.mergedProject.setGroups({QStringLiteral("dialogue")});
        for (int conflictIndex = 0; conflictIndex < 100; ++conflictIndex) {
            labelqt::services::ProjectMergeConflict conflict;
            conflict.imageName = QStringLiteral("page%1.png").arg(conflictIndex);
            conflict.selectedCandidateIndex = 3;
            for (int index = 0; index < 20; ++index) {
                labelqt::services::ProjectMergeCandidate candidate;
                candidate.projectPath = QStringLiteral("/source%1/project.txt").arg(index);
                candidate.image = imageEntry(index % 2 ? redPath : bluePath);
                candidate.image.name = conflict.imageName;
                candidate.image.labels = {
                    labelqt::core::Label(QStringLiteral("Candidate text"), QStringLiteral("dialogue"), {0.5, 0.5})};
                candidate.labelCount = 1;
                conflict.candidates.append(candidate);
            }
            plan.mergedProject.images().append(conflict.candidates.at(conflict.selectedCandidateIndex).image);
            plan.conflicts.append(conflict);
        }
        ProjectMergeDialog dialog(std::move(plan), {});
        dialog.showNormal();
        dialog.resize(900, 600);
        const auto canvases = dialog.findChildren<ImageCanvas*>();
        QCOMPARE(canvases.size(), 2);
        const auto caches = dialog.findChildren<labelqt::services::ImagePageCache*>();
        QCOMPARE(caches.size(), 2);
        QSignalSpy firstLoads(caches[0], &labelqt::services::ImagePageCache::imageLoaded);
        QSignalSpy secondLoads(caches[1], &labelqt::services::ImagePageCache::imageLoaded);
        QTRY_COMPARE(firstLoads.count(), 1);
        QTRY_COMPARE(secondLoads.count(), 1);
        QTRY_VERIFY(!canvasImage(canvases[0]).isNull());
        QTRY_VERIFY(!canvasImage(canvases[1]).isNull());
        QVERIFY(captureWidget(dialog, QStringLiteral("merge")));
        auto* conflicts = dialog.findChild<QListWidget*>(QStringLiteral("mergeConflicts"));
        auto* source = dialog.findChild<QComboBox*>(QStringLiteral("mergeSourceChoice"));
        auto* comparison = dialog.findChild<QComboBox*>(QStringLiteral("mergePreviewChoice1"));
        QCOMPARE(source->currentIndex(), 3);
        comparison->setCurrentIndex(7);
        QCOMPARE(dialog.selectedCandidateIndexes()[0], 3);
        source->setCurrentIndex(9);
        QCOMPARE(dialog.selectedCandidateIndexes()[0], 9);
        conflicts->setCurrentRow(40);
        QCOMPARE(source->currentIndex(), 3);
        source->setCurrentIndex(12);
        conflicts->setCurrentRow(0);
        QCOMPARE(source->currentIndex(), 9);
        QCOMPARE(comparison->currentIndex(), 7);
        QCOMPARE(dialog.selectedCandidateIndexes()[40], 12);
        QCOMPARE(dialog.findChildren<ImageCanvas*>(), canvases);
        QTRY_VERIFY(!canvasImage(canvases[0]).isNull());
        QTRY_VERIFY(!canvasImage(canvases[1]).isNull());
        QTest::qWait(50);
        canvases[0]->restoreView(200, QPointF(0.4, 0.6));
        QTRY_COMPARE(canvases[1]->zoomPercent(), 200);
        QVERIFY(QLineF(canvases[0]->normalizedViewCenter(), canvases[1]->normalizedViewCenter()).length() < 0.03);
        conflicts->setCurrentRow(1);
        conflicts->setCurrentRow(0);
        QTRY_VERIFY(!canvasImage(canvases[0]).isNull());
        QTRY_COMPARE(canvases[0]->zoomPercent(), 200);
        QVERIFY(dialog.minimumSizeHint().width() < 900);
        dialog.setShouldOpenMergedProjectAfterSave(true);
        QVERIFY(dialog.shouldOpenMergedProjectAfterSave());
        dialog.setShouldOpenMergedProjectAfterSave(false);
        QVERIFY(!dialog.shouldOpenMergedProjectAfterSave());
        dialog.done(QDialog::Accepted);
        QCOMPARE(dialog.selectedCandidateIndexes()[0], 9);
        for (auto* canvas : canvases)
            QVERIFY(canvasImage(canvas).isNull());
    }

    void previewCoalescesRapidSwitchesAndDiscardsOldResult()
    {
        QTemporaryDir dir;
        const QString first = dir.filePath(QStringLiteral("first.png"));
        const QString last = dir.filePath(QStringLiteral("last.png"));
        QImage image(40, 50, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(first));
        image.fill(Qt::blue);
        QVERIFY(image.save(last));
        const auto gate = std::make_shared<DecodeGate>();
        ReleaseDecoders release{gate};
        MergeCandidatePreview preview({}, {}, nullptr, gatedLoader(gate));
        auto* canvas = preview.findChild<ImageCanvas*>();
        preview.setCandidate(imageEntry(first));
        QTRY_COMPARE(gate->started.load(), 1);
        for (int i = 0; i < 200; ++i)
            preview.setCandidate(imageEntry(i == 199 ? last : first));
        QCOMPARE(gate->started.load(), 1);
        preview.restoreView(175, QPointF(0.3, 0.7));
        gate->permits.release();
        QTRY_COMPARE(gate->started.load(), 2);
        QVERIFY(canvasImage(canvas).isNull());
        gate->permits.release();
        QTRY_VERIFY(!canvasImage(canvas).isNull());
        QCOMPARE(canvasImage(canvas).pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(canvas->zoomPercent(), 175);
        QCOMPARE(gate->started.load(), 2);
    }

    void previewBoundsDecodedSizeAndHandlesMissingImages()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("large.png"));
        QImage image(2400, 1800, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        MergeCandidatePreview preview({}, {});
        auto* canvas = preview.findChild<ImageCanvas*>();
        auto* cache = preview.findChild<labelqt::services::ImagePageCache*>();
        QSignalSpy loaded(cache, &labelqt::services::ImagePageCache::imageLoaded);
        preview.setCandidate(imageEntry(path));
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY(canvasImage(canvas).width() <= 1600);
        QVERIFY(canvasImage(canvas).height() <= 1600);
        QVERIFY(!canvasImage(canvas).isNull());
        preview.setCandidate(imageEntry(dir.filePath(QStringLiteral("missing.png"))));
        QTRY_COMPARE(loaded.count(), 2);
        QVERIFY(canvasImage(canvas).isNull());
        const QString oversizedPath = dir.filePath(QStringLiteral("oversized.ppm"));
        QFile oversized(oversizedPath);
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        oversized.write("P6\n100000 100000\n255\n");
        oversized.close();
        preview.setCandidate(imageEntry(oversizedPath));
        QTRY_COMPARE(loaded.count(), 3);
        QVERIFY(canvasImage(canvas).isNull());
    }

    void previewDestructionDuringDecode()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("image.png"));
        QImage image(20, 20, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        const auto gate = std::make_shared<DecodeGate>();
        ReleaseDecoders release{gate};
        auto* preview = new MergeCandidatePreview({}, {}, nullptr, gatedLoader(gate));
        QPointer<ImageCanvas> canvas = preview->findChild<ImageCanvas*>();
        preview->setCandidate(imageEntry(path));
        QTRY_COMPARE(gate->started.load(), 1);
        preview->setCandidate(imageEntry(path));
        delete preview;
        QVERIFY(canvas.isNull());
        gate->permits.release();
        QTRY_COMPARE(gate->finished.load(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(gate->started.load(), 1);
    }

    void mergeDestructionWithoutDoneAndEmptyPlan()
    {
        for (int i = 0; i < 10; ++i) {
            labelqt::services::ProjectMergePlan plan;
            labelqt::services::ProjectMergeConflict conflict;
            conflict.candidates.resize(20);
            plan.conflicts.fill(conflict, 20);
            auto* dialog = new ProjectMergeDialog(std::move(plan), {});
            QPointer<ImageCanvas> canvas = dialog->findChild<ImageCanvas*>();
            delete dialog;
            QVERIFY(canvas.isNull());
            QCoreApplication::processEvents();
        }
        ProjectMergeDialog empty({}, {});
        QVERIFY(empty.selectedCandidateIndexes().isEmpty());
        empty.done(QDialog::Rejected);
    }
};

QTEST_MAIN(ResourceUiTests)
#include "ResourceUiTests.moc"
