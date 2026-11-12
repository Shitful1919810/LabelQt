#include "core/LabelPlusDocument.h"
#include "mobile/MobileDocument.h"
#include "mobile/MobileSearchController.h"
#include "mobile/MobileReviewController.h"
#include "mobile/MobileWorkspaceController.h"
#include "platform/android/AndroidFileBridge.h"
#include "services/proofread/TextDiffHtmlRenderer.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class MobileShellTests : public QObject {
    Q_OBJECT
private slots:
    void workspaceDiagnosticIsPlainWrappedAndKeepsMenuAccessible()
    {
        labelqt::mobile::MobileWorkspaceModel model;
        labelqt::services::WorkspaceInfo entry;
        entry.id = "broken";
        entry.title = "Retained project";
        entry.diagnostic = "<b>Cannot inspect this project</b>: " + QString(120, QLatin1Char('x'));
        model.replace({entry});
        QQmlEngine engine;
        QQuickWindow window;
        QQmlComponent component(&engine, QUrl::fromLocalFile(LABELQT_SOURCE_DIR "/src/mobile/qml/WorkspaceBrowser.qml"));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({
            {"workspaceModel", QVariant::fromValue(&model)}, {"showHeading", false}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* browser = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(browser);
        browser->setParentItem(window.contentItem());
        browser->setSize({320, 640});
        window.resize(320, 640);
        window.show();
        const auto findItem = [](auto&& self, QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name)
                return parent;
            for (auto* child : parent->childItems())
                if (auto* found = self(self, child, name))
                    return found;
            return nullptr;
        };
        QTRY_VERIFY(findItem(findItem, browser, "workspaceDiagnostic_broken"));
        auto* warning = findItem(findItem, browser, "workspaceDiagnostic_broken");
        auto* manage = findItem(findItem, browser, "manageWorkspace_broken");
        auto* open = findItem(findItem, browser, "openWorkspace_broken");
        QVERIFY(warning && manage && open);
        for (const int width : {320, 220, 840}) {
            window.resize(width, 640);
            browser->setWidth(width);
            QTest::qWait(50);
            QVERIFY(warning->isVisible());
            QCOMPARE(warning->property("textFormat").toInt(), int(Qt::PlainText));
            QCOMPARE(warning->property("text").toString(), entry.diagnostic);
            const auto warningBounds = warning->mapRectToScene(warning->boundingRect());
            const auto menuBounds = manage->mapRectToScene(manage->boundingRect());
            const auto rowBounds = open->mapRectToScene(open->boundingRect());
            QVERIFY(warningBounds.right() <= menuBounds.left());
            QVERIFY(warningBounds.bottom() <= rowBounds.bottom());
            QVERIFY(menuBounds.right() <= width);
            QVERIFY(manage->width() >= 48 && manage->height() >= 48);
            if (width == 220)
                QVERIFY(warning->height() > 24);
        }
        entry.diagnostic.clear();
        model.replace({entry});
        QTRY_VERIFY(!warning->isVisible());
        QCOMPARE(findItem(findItem, browser, "manageWorkspace_broken"), manage);
        entry.diagnostic = "Updated inspection failure";
        model.replace({entry});
        QTRY_VERIFY(warning->isVisible());
        QCOMPARE(warning->property("text").toString(), entry.diagnostic);
    }

    void searchRangeTracksProjectAndEquivalentQueriesKeepResults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto writeProject = [&](const QString& name, int count) {
            labelqt::core::Project project;
            project.setFilePath(directory.filePath(name + ".txt"));
            project.setGroups({"Dialogue"});
            for (int i = 0; i < count; ++i) {
                labelqt::core::ImageEntry page;
                page.name = QString::number(i) + ".png";
                page.path = directory.filePath(page.name);
                page.labels.append(labelqt::core::Label("needle", "Dialogue", {0.5, 0.5}));
                project.images().append(page);
            }
            QFile output(project.filePath());
            if (!output.open(QIODevice::WriteOnly))
                return QString{};
            output.write(labelqt::core::LabelPlusDocument::serialize(project).toUtf8());
            return project.filePath();
        };
        const auto large = writeProject("large", 10);
        const auto small = writeProject("small", 3);
        QVERIFY(!large.isEmpty() && !small.isEmpty());
        labelqt::mobile::MobileDocument document;
        labelqt::mobile::MobileSearchController search(document);
        MobileReviewController review(document.controller(), document.undoStack());
        QVERIFY(document.openProject(large));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(LABELQT_SOURCE_DIR "/src/mobile/qml/InspectionPanel.qml"));
        std::unique_ptr<QObject> panel(component.createWithInitialProperties({
            {"document", QVariant::fromValue(&document)}, {"search", QVariant::fromValue(&search)},
            {"review", QVariant::fromValue(&review)},
            {"finishEditing", QVariant::fromValue(engine.evaluate("(function() { return true; })"))}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        auto* scope = panel->findChild<QObject*>("searchPageScope");
        auto* first = panel->findChild<QObject*>("searchFirstPage");
        auto* last = panel->findChild<QObject*>("searchLastPage");
        auto* input = panel->findChild<QObject*>("labelSearchInput");
        QVERIFY(scope && first && last && input);
        scope->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, 2)));
        first->setProperty("value", 8);
        input->setProperty("text", "needle");
        QTRY_VERIFY(!search.busy());
        QCOMPARE(search.results().size(), 3);
        QVERIFY(document.openProject(small));
        QTRY_COMPARE(first->property("value").toInt(), 3);
        QCOMPARE(last->property("value").toInt(), 3);
        QTRY_VERIFY(!search.busy());
        QCOMPARE(search.results().size(), 1);
        QCOMPARE(search.results().first().toMap().value("pageName").toString(), "2.png");

        // Programmatic expansion also updates the effective range, without a user signal.
        QVERIFY(document.openProject(large));
        QTRY_COMPARE(last->property("value").toInt(), 10);
        QTRY_VERIFY(!search.busy());
        QCOMPARE(search.results().size(), 8);
        QVERIFY(search.navigate(0));
        QTest::qWait(250);
        QSignalSpy changed(&search, &labelqt::mobile::MobileSearchController::changed);
        const auto results = search.results();
        search.configure("needle", false, 2, 2, 9, true, {"ignored"});
        document.setTitle("Only a display change");
        QTest::qWait(250);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(search.results(), results);
        QCOMPARE(search.current(), 0);

        search.configure("needle", false, 0, 0, -1, false, {"Dialogue", "Other"});
        QTRY_VERIFY(!search.busy());
        changed.clear();
        search.configure("needle", false, 0, 99, 100, false, {"Other", "Dialogue", "Dialogue"});
        QTest::qWait(250);
        QCOMPARE(changed.count(), 0);
    }

    void newlineMarkersRenderWithoutSymbolFonts_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QColor>("background");
        QTest::newRow("equal") << QString("\n") << QString("\n") << QColor("#475569");
        QTest::newRow("insert") << QString{} << QString("\n") << QColor("#14532d");
        QTest::newRow("delete") << QString("\n") << QString{} << QColor("#7f1d1d");
    }

    void newlineMarkersRenderWithoutSymbolFonts()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QFETCH(QColor, background);
        const auto html = labelqt::services::TextDiffHtmlRenderer::renderInlineDiff(before, after, {}, true);
        QVERIFY(!html.contains(QChar(0x21b5)));
        QCOMPARE(html.count("<img "), 1);
        QCOMPARE(html.count("<br/>"), 1);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("sampleHtml", html);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nText { width: 160; height: 64; textFormat: Text.RichText; text: sampleHtml }", QUrl{});
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(160, 64);
        window.setColor(Qt::white);
        std::unique_ptr<QObject> object(component.create());
        auto* item = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(item);
        item->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(150);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        int coloredPixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == background)
                    ++coloredPixels;
        QVERIFY2(coloredPixels > 30, qPrintable(QString::number(coloredPixels)));
    }

    void editorAndResponsiveLayout()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage page(800, 1200, QImage::Format_RGB32);
        page.fill(Qt::white);
        QPainter painter(&page);
        painter.setPen(Qt::black);
        painter.drawRect(40, 40, 720, 1120);
        painter.drawLine(40, 600, 760, 600);
        painter.end();
        QVERIFY(page.save(directory.filePath("page.png")));
        labelqt::core::Project project;
        project.setFilePath(directory.filePath("project.txt"));
        project.setGroups({QStringLiteral("Dialogue"), QStringLiteral("Background")});
        labelqt::core::ImageEntry entry;
        entry.name = QStringLiteral("page.png");
        entry.path = directory.filePath(entry.name);
        entry.labels.append(
            labelqt::core::Label(QStringLiteral("Hello\nworld"), QStringLiteral("Dialogue"), {0.3, 0.25}));
        project.images().append(entry);
        QFile output(project.filePath());
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(labelqt::core::LabelPlusDocument::serialize(project).toUtf8());
        output.close();

        labelqt::mobile::MobileDocument document;
        labelqt::mobile::MobileSearchController search(document);
        labelqt::mobile::MobileWorkspaceController workspace(document, directory.filePath("workspaces"));
        labelqt::platform::AndroidFileBridge files;
        MobileReviewController review(document.controller(), document.undoStack());
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& errors) {
            for (const auto& error : errors)
                warnings.append(error.toString());
        });
        engine.rootContext()->setContextProperty("mobileDocument", &document);
        engine.rootContext()->setContextProperty("mobileSearch", &search);
        engine.rootContext()->setContextProperty("workspaceController", &workspace);
        engine.rootContext()->setContextProperty("androidFiles", &files);
        engine.rootContext()->setContextProperty("mobileReview", &review);
        engine.load(QUrl::fromLocalFile(QStringLiteral(LABELQT_SOURCE_DIR "/src/mobile/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* home = window->findChild<QQuickItem*>(QStringLiteral("homeProjects"));
        QVERIFY(home && home->isVisible());
        auto* aboutAction = window->findChild<QObject*>(QStringLiteral("aboutAction"));
        auto* shareLast = window->findChild<QObject*>(QStringLiteral("shareLastExportAction"));
        QVERIFY(shareLast);
        QVERIFY(!shareLast->property("visible").toBool());
        auto* aboutDialog = window->findChild<QObject*>(QStringLiteral("aboutDialog"));
        auto* aboutVersion = window->findChild<QQuickItem*>(QStringLiteral("aboutVersion"));
        QVERIFY(aboutAction && aboutDialog && aboutVersion);
        QTRY_COMPARE(aboutAction->property("y").toReal(), 0.0);
        QVERIFY(QMetaObject::invokeMethod(aboutAction, "triggered"));
        QTRY_VERIFY(aboutDialog->property("opened").toBool());
        QVERIFY(aboutVersion->property("text").toString().contains(QCoreApplication::applicationVersion()));
        const auto aboutScreenshots = qEnvironmentVariable("LABELQT_SCREENSHOT_DIR");
        if (!aboutScreenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(aboutScreenshots));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QDir(aboutScreenshots).filePath("about.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(aboutDialog, "close"));
        QTRY_VERIFY(!aboutDialog->property("visible").toBool());
        workspace.importLocal(QUrl::fromLocalFile(project.filePath()));
        QTRY_COMPARE(workspace.workspaces()->rowCount(), 1);
        QTRY_VERIFY(!workspace.busy());
        const auto key = workspace.workspaces()->entries().first().id;
        const auto screenshots = qEnvironmentVariable("LABELQT_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            for (const auto size : {QSize(320, 640), QSize(1080, 800)}) {
                window->resize(size);
                QTest::qWait(100);
                QVERIFY(
                    window->grabWindow().save(QDir(screenshots).filePath(QString("home-%1.png").arg(size.width()))));
            }
        }
        QVERIFY(QMetaObject::invokeMethod(home, "openRequested", Q_ARG(QString, key)));
        QVERIFY(document.opened());
        QVERIFY(!home->isVisible());
        document.setTitle(QStringLiteral("Preview project"));
        document.selectLabel(document.controller().project().images().first().labels.first().id().toString());
        QTRY_VERIFY(window->findChild<QQuickItem*>(QStringLiteral("labelTextEditor")));
        auto* editor = window->findChild<QQuickItem*>(QStringLiteral("labelTextEditor"));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Hello\nworld"));
        editor->forceActiveFocus();
        QVERIFY(editor->setProperty("text", QStringLiteral("Updated\ntext")));
        QVERIFY(document.commitText());
        QVERIFY(document.save());
        QCOMPARE(document.controller().project().images().first().labels.first().text(),
                 QStringLiteral("Updated\ntext"));
        document.undo();
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Hello\nworld"));

        const auto screenshotDirectory = qEnvironmentVariable("LABELQT_SCREENSHOT_DIR");
        auto* canvas = window->findChild<QQuickItem*>(QStringLiteral("projectCanvas"));
        QVERIFY(canvas);
        for (const auto size : {QSize(360, 800), QSize(1080, 800), QSize(360, 480), QSize(360, 360), QSize(800, 320)}) {
            window->resize(size);
            QTest::qWait(100);
            QVERIFY(editor->width() > 150);
            QVERIFY(editor->height() > 40);
            QVERIFY2(canvas->height() >= 120, qPrintable(QString::number(canvas->height())));
            const auto bounds = editor->mapRectToItem(window->contentItem(), editor->boundingRect());
            QVERIFY2(bounds.left() >= 0 && bounds.right() <= size.width() + 1,
                     qPrintable(QString::number(bounds.right())));
            QVERIFY(bounds.bottom() <= size.height() + 1);
            const auto imageBounds = canvas->mapRectToItem(window->contentItem(), canvas->boundingRect());
            QVERIFY(imageBounds.bottom() <= bounds.top());
            const auto screenshot = window->grabWindow();
            QVERIFY(!screenshot.isNull());
            if (!screenshotDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(screenshotDirectory));
                QVERIFY(screenshot.save(
                    QDir(screenshotDirectory)
                        .filePath(QStringLiteral("mobile-%1x%2.png").arg(size.width()).arg(size.height()))));
            }
        }
        // Exercise normal chrome too: all packaged icon sources must load without font fallback.
        canvas->forceActiveFocus();
        window->resize({360, 800});
        QTest::qWait(100);
        QVERIFY(!window->property("editingText").toBool());
        auto* title = window->findChild<QQuickItem*>(QStringLiteral("projectTitle"));
        QVERIFY(title && title->isVisible());
        QVERIFY(title->width() >= 100);
        QVERIFY(title->property("text").toString().startsWith(QStringLiteral("Preview project")));
        auto* focusMarker = window->findChild<QQuickItem*>(QStringLiteral("focusMarkerButton"));
        // Android Back can dismiss the IME while leaving TextArea focus intact.
        // Focus alone must not hide navigation or leave an armed canvas mode without an exit.
        window->setProperty("canvasTool", 1);
        editor->forceActiveFocus();
        QVERIFY(window->property("editingText").toBool());
        QVERIFY(!window->property("compactEditor").toBool());
        auto* canvasTools = window->findChild<QQuickItem*>("canvasTools");
        auto* pageNavigation = window->findChild<QQuickItem*>("pageNavigation");
        auto* mainTabs = window->findChild<QQuickItem*>("mainTabs");
        auto* browseTool = window->findChild<QQuickItem*>("browseTool");
        QVERIFY(canvasTools && pageNavigation && mainTabs && browseTool);
        QVERIFY(canvasTools->isVisible() && pageNavigation->isVisible() && mainTabs->isVisible());
        window->resize(800, 320);
        QTest::qWait(100);
        QVERIFY(window->property("compactEditor").toBool());
        QVERIFY(canvasTools->isVisible());
        QVERIFY(canvas->height() > 40);
        const auto toolBounds = canvasTools->mapRectToItem(window->contentItem(), canvasTools->boundingRect());
        QVERIFY(toolBounds.bottom() <= window->contentItem()->height());
        QVERIFY(QMetaObject::invokeMethod(browseTool, "clicked"));
        QCOMPARE(window->property("canvasTool").toInt(), 0);
        QVERIFY(!window->property("editingText").toBool());
        window->resize(360, 800);
        QVERIFY(focusMarker);
        window->setProperty("panel", 1);
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(focusMarker, "clicked"));
        QTRY_COMPARE(window->property("panel").toInt(), 0);
        auto* searchInput = window->findChild<QQuickItem*>(QStringLiteral("labelSearchInput"));
        QVERIFY(searchInput);
        window->setProperty("panel", 2);
        searchInput->setProperty("text", "Hello");
        QTRY_VERIFY(!search.busy());
        QCOMPARE(search.results().size(), 1);
        for (const auto size : {QSize(360, 800), QSize(800, 320), QSize(1080, 800)}) {
            window->resize(size);
            QTest::qWait(150);
            auto* results = window->findChild<QQuickItem*>(QStringLiteral("searchResults"));
            QVERIFY(results && results->isVisible());
            QVERIFY(results->height() > 0);
            QVERIFY(results->mapRectToScene(results->boundingRect()).bottom() <= size.height() + 1);
            if (!screenshotDirectory.isEmpty())
                QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath(
                    QString("search-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        window->setProperty("panel", 0);
        if (!screenshotDirectory.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath("mobile-browse.png")));

        QVERIFY(review.startRound());
        window->setProperty("panel", 2);
        auto* noteDialog = window->findChild<QObject*>("reviewNoteDialog");
        auto* noteText = window->findChild<QQuickItem*>("reviewNoteText");
        auto* noteScope = window->findChild<QQuickItem*>("noteTargetScope");
        auto* tabs = window->findChild<QQuickItem*>("inspectionTabs");
        QVERIFY(noteDialog && noteText && noteScope && tabs);
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "createNote"));
        QTRY_VERIFY(noteDialog->property("opened").toBool());
        QCOMPARE(noteScope->property("currentIndex").toInt(), 0);
        noteScope->setProperty("currentIndex", 1);
        noteText->setProperty("text", "Page comment <b>not HTML</b>");
        auto* noteSubmit = noteDialog->findChild<QQuickItem*>("submitDialogButton");
        QVERIFY(noteSubmit);
        for (const auto size : {QSize(360, 800), QSize(800, 320)}) {
            window->resize(size);
            QTest::qWait(150);
            const auto bounds = noteSubmit->mapRectToScene(noteSubmit->boundingRect());
            QVERIFY(bounds.top() >= 0 && bounds.bottom() <= size.height());
            if (!screenshotDirectory.isEmpty())
                QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath(
                    QString("note-editor-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "submitRequested"));
        QTRY_VERIFY(!noteDialog->property("visible").toBool());
        QCOMPARE(review.notes().size(), 1);
        QVERIFY(review.notes().first().toMap().value("labelKey").toString().isEmpty());
        QVERIFY(review.addNote(document.pageId(), document.selectedLabel(), "Explain this wording"));
        tabs->setProperty("currentIndex", 2);
        for (const auto size : {QSize(360, 800), QSize(800, 320), QSize(1080, 800)}) {
            window->resize(size);
            QTest::qWait(150);
            auto* notes = window->findChild<QQuickItem*>("proofreadNotes");
            QVERIFY(notes && notes->isVisible() && notes->height() >= 48);
            QVERIFY(notes->mapRectToScene(notes->boundingRect()).bottom() <= size.height() + 1);
            if (!screenshotDirectory.isEmpty())
                QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath(
                    QString("notes-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "createNote"));
        QTRY_VERIFY(noteDialog->property("opened").toBool());
        noteText->setProperty("text", "Stale draft");
        QVERIFY(review.addNote(document.pageId(), {}, "Intervening edit"));
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "submitRequested"));
        QVERIFY(noteDialog->property("visible").toBool());
        QCOMPARE(review.notes().size(), 3);
        QVERIFY(!review.error().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "reject"));
        QTRY_VERIFY(!noteDialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "changeReviewState"));
        auto* roundDialog = window->findChild<QObject*>("reviewRoundDialog");
        QVERIFY(roundDialog);
        QTRY_VERIFY(roundDialog->property("opened").toBool());
        QCOMPARE(review.roundState(), "active");
        QVERIFY(QMetaObject::invokeMethod(roundDialog, "submitRequested"));
        QTRY_VERIFY(!roundDialog->property("visible").toBool());
        QCOMPARE(review.roundState(), "completed");
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "showNote", Q_ARG(QVariant, review.notes().first())));
        QTRY_VERIFY(noteDialog->property("opened").toBool());
        QVERIFY(noteText->property("readOnly").toBool());
        QVERIFY(QMetaObject::invokeMethod(noteDialog, "reject"));
        QTRY_VERIFY(!noteDialog->property("visible").toBool());
        window->setProperty("panel", 0);

        auto* pdfAction = window->findChild<QObject*>("sharePdfAction");
        auto* pdfDialog = window->findChild<QObject*>("sharePdfDialog");
        auto* pdfName = window->findChild<QQuickItem*>("reportFileName");
        QVERIFY(pdfAction && pdfDialog && pdfName);
        QVERIFY(pdfAction->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(pdfAction, "triggered"));
        QTRY_VERIFY(pdfDialog->property("opened").toBool());
        QCOMPARE(pdfName->property("text").toString(), workspace.suggestedReportName());
        pdfName->setProperty("text", "../invalid");
        QVERIFY(QMetaObject::invokeMethod(pdfDialog, "submitRequested"));
        QVERIFY(pdfDialog->property("visible").toBool());
        QVERIFY(!workspace.busy());
        auto* pdfSubmit = pdfDialog->findChild<QQuickItem*>("submitDialogButton");
        QVERIFY(pdfSubmit);
        for (const auto size : {QSize(360, 800), QSize(800, 320)}) {
            window->resize(size);
            QTest::qWait(100);
            const auto bounds = pdfSubmit->mapRectToScene(pdfSubmit->boundingRect());
            QVERIFY(bounds.top() >= 0 && bounds.bottom() <= size.height());
            if (!screenshotDirectory.isEmpty())
                QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath(
                    QString("share-pdf-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        QVERIFY(QMetaObject::invokeMethod(pdfDialog, "reject"));
        QTRY_VERIFY(!pdfDialog->property("visible").toBool());

        auto* drawer = window->findChild<QObject*>(QStringLiteral("projectsDrawer"));
        auto* openProjects = window->findChild<QQuickItem*>(QStringLiteral("openProjectsButton"));
        auto* closeProjects = window->findChild<QQuickItem*>(QStringLiteral("closeProjectsButton"));
        QVERIFY(drawer && openProjects && closeProjects);
        workspace.importLocal(QUrl::fromLocalFile(project.filePath()));
        QTRY_COMPARE(workspace.workspaces()->rowCount(), 2);
        QTRY_VERIFY(!workspace.busy());
        const auto selected = document.selectedLabel();
        for (const auto size : {QSize(320, 640), QSize(1080, 800)}) {
            window->resize(size);
            QTest::qWait(100);
            for (int method = 0; method < 4; ++method) {
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                  openProjects->mapToScene(openProjects->boundingRect().center()).toPoint());
                QTRY_VERIFY(drawer->property("opened").toBool());
                QVERIFY(drawer->property("width").toReal() <= size.width() - 48);
                const auto closeBounds = closeProjects->mapRectToScene(closeProjects->boundingRect());
                QVERIFY(closeBounds.left() >= 0 && closeBounds.right() <= size.width());
                QVERIFY(closeProjects->width() >= 48 && closeProjects->height() >= 48);
                if (!screenshotDirectory.isEmpty() && method == 0)
                    QVERIFY(window->grabWindow().save(
                        QDir(screenshotDirectory).filePath(QString("projects-%1.png").arg(size.width()))));
                if (method == 0)
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, closeBounds.center().toPoint());
                else if (method == 1)
                    QTest::keyClick(window, Qt::Key_Escape);
                else if (method == 2)
                    QTest::keyClick(window, Qt::Key_Back);
                else
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, {size.width() - 16, size.height() / 2});
                QTRY_VERIFY2(!drawer->property("visible").toBool(), qPrintable(QString("Close method %1").arg(method)));
                QCOMPARE(document.selectedLabel(), selected);
            }
        }
        auto* shareAction = window->findChild<QObject*>(QStringLiteral("shareZipAction"));
        auto* shareDialog = window->findChild<QObject*>(QStringLiteral("shareZipDialog"));
        auto* shareName = window->findChild<QQuickItem*>(QStringLiteral("shareZipName"));
        QVERIFY(shareAction && shareDialog && shareName);
        QVERIFY(QMetaObject::invokeMethod(shareAction, "triggered"));
        QTRY_VERIFY(shareDialog->property("opened").toBool());
        auto* footer = shareDialog->property("footer").value<QObject*>();
        QVERIFY(footer);
        QCOMPARE(footer->property("count").toInt(), 2);
        QCOMPARE(shareName->property("text").toString(), workspace.suggestedShareName());
        window->resize({360, 800});
        QTest::qWait(100);
        if (!screenshotDirectory.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath("share-name.png")));
        QVERIFY(shareName->setProperty("text", QStringLiteral("../invalid.zip")));
        auto* submit = shareDialog->findChild<QQuickItem*>(QStringLiteral("submitDialogButton"));
        QVERIFY(submit);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          submit->mapToScene(submit->boundingRect().center()).toPoint());
        QTest::qWait(300);
        QVERIFY(shareDialog->property("visible").toBool());
        QVERIFY(!workspace.busy());
        QVERIFY(!workspace.message().isEmpty());
        for (const auto size : {QSize(320, 480), QSize(800, 320)}) {
            window->resize(size);
            QTest::qWait(100);
            const auto bounds = submit->mapRectToScene(submit->boundingRect());
            QVERIFY(bounds.top() >= 0 && bounds.bottom() <= size.height());
            QVERIFY(bounds.left() >= 0 && bounds.right() <= size.width());
            if (!screenshotDirectory.isEmpty())
                QVERIFY(window->grabWindow().save(
                    QDir(screenshotDirectory)
                        .filePath(QString("share-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        QVERIFY(QMetaObject::invokeMethod(shareDialog, "close"));
        QVERIFY(workspace.exportProject("retry-share"));
        QTRY_VERIFY(!workspace.busy());
        QVERIFY2(!workspace.lastExport().isEmpty(), qPrintable(workspace.message()));
        auto* actions = window->findChild<QObject*>("projectActionsMenu");
        QVERIFY(actions);
        QVERIFY(QMetaObject::invokeMethod(actions, "open"));
        QTRY_VERIFY(shareLast->property("visible").toBool());
        QSignalSpy exportReady(&workspace, &labelqt::mobile::MobileWorkspaceController::exportReady);
        const auto exported = workspace.lastExport();
        QVERIFY(QMetaObject::invokeMethod(shareLast, "triggered"));
        QCOMPARE(exportReady.count(), 1);
        QCOMPARE(exportReady.first().first().toString(), exported);
        QVERIFY(QFile::remove(exported));
        QVERIFY(QMetaObject::invokeMethod(shareLast, "triggered"));
        QCOMPARE(exportReady.count(), 1);
        QVERIFY(!shareLast->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(actions, "close"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral("0.1.0-test"));
    QQuickStyle::setStyle(QStringLiteral("Material"));
    MobileShellTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "MobileShellTests.moc"
