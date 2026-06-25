#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"

#include "core/AppPreferences.h"
#include "core/TranslationManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStyle>
#include <QStyleFactory>
#include <QTranslator>

namespace {
void installTranslator(QApplication& app, const QString& language)
{
    auto* translator = new QTranslator(&app);

    if (labelqt::core::loadApplicationTranslator(*translator, language)) {
        app.installTranslator(translator);
        return;
    }

    translator->deleteLater();
}

void applyApplicationStyle(const QString& styleName)
{
    if (!styleName.isEmpty() && QStyleFactory::keys().contains(styleName, Qt::CaseInsensitive)) {
        QApplication::setStyle(styleName);
    }
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setProperty("labelqt.defaultStyle", app.style()->objectName());
    const labelqt::core::AppPreferencesLoadResult preferences =
        labelqt::core::AppPreferences::loadWithDiagnostics();
    applyApplicationStyle(preferences.preferences.applicationStyle());
    labelqt::ui::applyApplicationTheme(preferences.preferences.applicationTheme());
    QApplication::setApplicationName("LabelQt");
    QApplication::setOrganizationName("LabelQt");
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    installTranslator(app, preferences.preferences.applicationLanguage());

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "LabelPlus text project editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QCoreApplication::translate("main", "LabelPlus text project to open."));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList positionalArguments = parser.positionalArguments();
    if (!positionalArguments.isEmpty()) {
        window.openProjectFile(QFileInfo(positionalArguments.first()).absoluteFilePath());
    }
    else {
        window.openMostRecentProject();
    }

    return QApplication::exec();
}
