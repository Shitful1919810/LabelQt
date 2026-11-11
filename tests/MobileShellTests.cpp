#include "core/LabelPlusDocument.h"
#include "mobile/MobileDocument.h"
#include "mobile/MobileReviewController.h"
#include "mobile/MobileWorkspaceController.h"
#include "platform/android/AndroidFileBridge.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class MobileShellTests : public QObject {
    Q_OBJECT
private slots:
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
        auto* aboutDialog = window->findChild<QObject*>(QStringLiteral("aboutDialog"));
        auto* aboutVersion = window->findChild<QQuickItem*>(QStringLiteral("aboutVersion"));
        QVERIFY(aboutAction && aboutDialog && aboutVersion);
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
        if (!screenshotDirectory.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(screenshotDirectory).filePath("mobile-browse.png")));

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
