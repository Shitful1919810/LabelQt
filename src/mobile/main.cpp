#include "core/AppPreferences.h"
#include "core/TranslationManager.h"
#include "mobile/MobileDocument.h"
#include "mobile/MobileSearchController.h"
#include "mobile/MobileReviewController.h"
#include "mobile/MobileWorkspaceController.h"
#include "platform/android/AndroidFileBridge.h"
#include "services/proofread/TextDiffService.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QInputMethod>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("LabelQt");
    app.setApplicationName("LabelQtMobile");
    app.setApplicationVersion(QStringLiteral(LABELQT_MOBILE_VERSION));
    QQuickStyle::setStyle("Material");
    QImageReader::setAllocationLimit(128);
    const auto preferences = labelqt::core::AppPreferences::loadWithDiagnostics().preferences;
    labelqt::services::TextDiffService::setCleanupMode(preferences.textDiffCleanupMode());
    QTranslator translator;
    if (labelqt::core::loadApplicationTranslator(translator, preferences.applicationLanguage()))
        app.installTranslator(&translator);

    // The OS may return an alias (e.g. Android's /data/user/0). Resolve only this
    // trusted root; transfer inputs and descendants must still reject links.
    const auto dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto workspaceRoot = QDir::isAbsolutePath(dataLocation) && QDir().mkpath(dataLocation)
                                   ? QFileInfo(dataLocation).canonicalFilePath()
                                   : QString{};
    if (workspaceRoot.isEmpty()) {
        qCritical("Cannot initialize the application private data directory.");
        return 1;
    }

    labelqt::mobile::MobileDocument document;
    labelqt::mobile::MobileSearchController search(document);
    document.setGroupStyles(preferences.groupStyles());
    labelqt::mobile::MobileWorkspaceController workspace(document, workspaceRoot);
    labelqt::platform::AndroidFileBridge files;
    MobileReviewController review(document.controller(), document.undoStack());
    QObject::connect(&document, &labelqt::mobile::MobileDocument::edited, &review, &MobileReviewController::refresh);
    QObject::connect(&review, &MobileReviewController::edited, &document, [&document] {
        document.refresh();
        document.save();
    });
    QObject::connect(&files, &labelqt::platform::AndroidFileBridge::fileReceived, &workspace,
                     &labelqt::mobile::MobileWorkspaceController::importReceived);
    QObject::connect(&files, &labelqt::platform::AndroidFileBridge::failure, &workspace,
                     &labelqt::mobile::MobileWorkspaceController::showMessage);
#ifdef Q_OS_ANDROID
    QObject::connect(&workspace, &labelqt::mobile::MobileWorkspaceController::exportReady, &files,
                     &labelqt::platform::AndroidFileBridge::shareFile);
#endif
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &document,
                     [&document](Qt::ApplicationState state) {
                         if (state != Qt::ApplicationActive) {
                             QGuiApplication::inputMethod()->commit();
                             document.save();
                         }
                     });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &document, [&document] { document.save(); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mobileDocument", &document);
    engine.rootContext()->setContextProperty("mobileSearch", &search);
    engine.rootContext()->setContextProperty("workspaceController", &workspace);
    engine.rootContext()->setContextProperty("androidFiles", &files);
    engine.rootContext()->setContextProperty("mobileReview", &review);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("LabelQt.Mobile", "Main");
    if (app.arguments().size() > 1)
        workspace.importLocal(QUrl::fromLocalFile(app.arguments().at(1)));
    return app.exec();
}
