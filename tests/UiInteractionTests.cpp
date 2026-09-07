#include "core/AppPreferences.h"
#include "ui/MainWindowShortcutController.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include "ui/proofread/ReviewStatusDelegate.h"
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QWidget>

class UiInteractionTests : public QObject {
    Q_OBJECT
private slots:
    void bundledAndBuiltInPreferencesAgree()
    {
        QFile file(QStringLiteral(LABELQT_SOURCE_DIR "/preference.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto loaded = labelqt::core::AppPreferences::loadFromJson(file.readAll());
        QVERIFY(loaded.warnings.isEmpty());
        QCOMPARE(loaded.preferences.toJsonDocument(), labelqt::core::AppPreferences{}.toJsonDocument());
    }

    void navigationOnlyInterceptsLabelWorkspace()
    {
        QWidget window;
        QPlainTextEdit search(&window), labelEditor(&window);
        MainWindowShortcutController controller(&window);
        controller.setLabelWorkspaceWidgets({&labelEditor});
        int next = 0, previousPage = 0;
        MainWindowShortcutController::Callbacks callbacks;
        callbacks.selectNextLabel = [&] { ++next; };
        callbacks.selectPreviousPage = [&] { ++previousPage; };
        controller.setCallbacks(callbacks);
        for (const auto type : {QEvent::ShortcutOverride, QEvent::KeyPress}) {
            QKeyEvent tab(type, Qt::Key_Tab, Qt::NoModifier);
            QKeyEvent left(type, Qt::Key_Left, Qt::ControlModifier);
            QVERIFY(!controller.handleGlobalShortcut(&search, &tab));
            QVERIFY(!controller.handleGlobalShortcut(search.viewport(), &left));
            QVERIFY(controller.handleGlobalShortcut(&labelEditor, &tab));
            QVERIFY(controller.handleGlobalShortcut(labelEditor.viewport(), &left));
            QKeyEvent copy(type, Qt::Key_C, Qt::ControlModifier);
            QVERIFY(!controller.handleGlobalShortcut(labelEditor.viewport(), &copy));
        }
        QCOMPARE(next, 1);
        QCOMPARE(previousPage, 1);
        auto* transient = new QWidget(&window);
        controller.setLabelWorkspaceWidgets({transient});
        delete transient;
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QVERIFY(!controller.handleGlobalShortcut(&labelEditor, &tab));
    }

    void reviewColorsSurviveSelection()
    {
        QWidget widget;
        ReviewStatusDelegate delegate(&widget);
        QStandardItemModel model(1, 1);
        const QVector<QColor> colors{QColor("#db505c"), QColor("#299c68"), QColor("#d69a19")};
        for (int status = 0; status < colors.size(); ++status) {
            model.setData(model.index(0, 0), status, ProofreadChangeTableModel::ReviewStatusRole);
            model.setData(model.index(0, 0), QStringLiteral("Review state"));
            for (bool selected : {false, true}) {
                QImage image(240, 40, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QStyleOptionViewItem option;
                option.initFrom(&widget);
                option.widget = &widget;
                option.rect = image.rect();
                if (selected)
                    option.state |= QStyle::State_Selected;
                QPainter painter(&image);
                delegate.paint(&painter, option, model.index(0, 0));
                painter.end();
                int coloredPixels = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < 36; ++x)
                        coloredPixels += image.pixelColor(x, y).rgb() == colors[status].rgb();
                QVERIFY2(coloredPixels > 8, "Review state accent must remain visible in selected rows");
            }
        }
    }
};
QTEST_MAIN(UiInteractionTests)
#include "UiInteractionTests.moc"
