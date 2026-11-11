#include "core/Label.h"
#include "mobile/MobileLabelModel.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTextureProvider>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTouchEvent>
#include <QWheelEvent>
#include <atomic>
#include <memory>

using labelqt::core::Label;
using labelqt::mobile::MobileLabelModel;

namespace {
constexpr int frameDelay = 25;
const QColor topLeftColor{QStringLiteral("#e7f3ef")};
const QColor topRightColor{QStringLiteral("#eff0f6")};

QString labelKey(const Label& label)
{
    return label.id().toString(QUuid::WithoutBraces);
}

bool writeFixture(const QString& path, QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    if (image.isNull())
        return false;
    QPainter painter(&image);
    const int halfWidth = size.width() / 2;
    const int halfHeight = size.height() / 2;
    painter.fillRect(0, 0, halfWidth, halfHeight, topLeftColor);
    painter.fillRect(halfWidth, 0, size.width() - halfWidth, halfHeight, topRightColor);
    painter.fillRect(0, halfHeight, halfWidth, size.height() - halfHeight, QColor("#f6eedc"));
    painter.fillRect(halfWidth, halfHeight, size.width() - halfWidth, size.height() - halfHeight, QColor("#f0e5e6"));
    painter.setPen(QPen(QColor("#444444"), 4));
    painter.drawRect(image.rect().adjusted(8, 8, -9, -9));
    painter.end();
    return image.save(path);
}

bool nearColor(QColor actual, QColor expected)
{
    return qAbs(actual.red() - expected.red()) <= 4 && qAbs(actual.green() - expected.green()) <= 4 &&
           qAbs(actual.blue() - expected.blue()) <= 4 && actual.alpha() == 255;
}

void pointerRows()
{
    QTest::addColumn<bool>("touchInput");
    QTest::newRow("mouse") << false;
    QTest::newRow("touch") << true;
}

// Destroy the component before its window/engine, and those before the model. Child references
// are guarded because a model reset destroys the Repeater's marker objects synchronously.
struct CanvasHost {
    QVector<Label> labels{Label("First label", "Text", {0.5, 0.5}), Label("Second label", "Text", {0.8, 0.75})};
    MobileLabelModel model;
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::QtTest};
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> canvas;
    QPointer<QQuickItem> image;

    void refresh()
    {
        model.refresh(labels, {QStringLiteral("Text")}, {QStringLiteral("Text")});
    }

    QQuickItem* marker(int row) const
    {
        // Repeater delegates are visual children, not necessarily QObject children of the canvas.
        const auto name = QStringLiteral("projectMarker_") + labelKey(labels.at(row));
        for (auto* child : canvas->childItems()) {
            if (child->objectName() == name)
                return child;
        }
        return nullptr;
    }

    QPoint imagePoint(qreal x, qreal y) const
    {
        return image->mapToScene({x * image->width(), y * image->height()}).toPoint();
    }

    QPoint markerCenter(int row) const
    {
        const auto* item = marker(row);
        return item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
    }

    void resize(QSize size)
    {
        window.resize(size);
        canvas->setSize(QSizeF(size));
        window.requestUpdate();
        QTest::qWait(frameDelay);
    }
};
} // namespace

class MobileCanvasTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_qmlPath = QFINDTESTDATA("../src/mobile/qml/ProjectCanvas.qml");
        QVERIFY2(!m_qmlPath.isEmpty(), "ProjectCanvas.qml must be available in the source tree.");
        QVERIFY(writeFixture(m_directory.filePath("portrait.png"), {2400, 3600}));
        QVERIFY(writeFixture(m_directory.filePath("landscape.png"), {3600, 1800}));
        QVERIFY(writeFixture(m_directory.filePath("tall.png"), {600, 6000}));
        // Qt's test input registration owns this device for the application lifetime.
        m_touchDevice = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        QVERIFY(m_touchDevice);
    }

    void init()
    {
        m_warnings.clear();
        m_host = std::make_unique<CanvasHost>();
        connect(&m_host->engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError>& errors) {
            for (const auto& error : errors)
                m_warnings.append(error.toString());
        });
        m_host->refresh();
        m_host->window.resize(640, 480);
        QQmlComponent component(&m_host->engine, QUrl::fromLocalFile(m_qmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QVariantMap properties{
            {QStringLiteral("width"), 640},
            {QStringLiteral("height"), 480},
            {QStringLiteral("labelModel"), QVariant::fromValue(static_cast<QObject*>(&m_host->model))},
            {QStringLiteral("selectedLabel"), labelKey(m_host->labels.first())},
            {QStringLiteral("imageSource"), fixtureUrl("portrait.png")}};
        std::unique_ptr<QObject> created(component.createWithInitialProperties(properties));
        QVERIFY2(created, qPrintable(component.errorString()));
        auto* item = qobject_cast<QQuickItem*>(created.get());
        QVERIFY(item);
        created.release();
        m_host->canvas.reset(item);
        item->setParentItem(m_host->window.contentItem());
        m_host->image = item->findChild<QQuickItem*>(QStringLiteral("projectCanvasImage"));
        QVERIFY(m_host->image);
        m_host->window.show();
        m_host->window.requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(&m_host->window));
        QTRY_VERIFY(m_host->image->isVisible());
        QTRY_VERIFY(m_host->image->width() > 0);
        QTRY_VERIFY(m_host->marker(0));
        QTRY_VERIFY(m_host->marker(1));
        QVERIFY(QMetaObject::invokeMethod(item, "resetView"));
        QTest::qWait(frameDelay);
        QCOMPARE(m_host->window.rendererInterface()->graphicsApi(), QSGRendererInterface::Software);
    }

    void cleanup()
    {
        if (m_host) {
            QTouchEvent cancel(QEvent::TouchCancel, m_touchDevice);
            QCoreApplication::sendEvent(&m_host->window, &cancel);
            m_host.reset();
        }
        QCoreApplication::processEvents();
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join('\n')));
    }

    void previewPreservesAspectAndDecodeBudget_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<qreal>("aspect");
        QTest::newRow("portrait") << QStringLiteral("portrait.png") << qreal(2.0 / 3.0);
        QTest::newRow("landscape") << QStringLiteral("landscape.png") << qreal(2.0);
        QTest::newRow("tall-page") << QStringLiteral("tall.png") << qreal(0.1);
    }

    void previewPreservesAspectAndDecodeBudget()
    {
        QFETCH(QString, name);
        QFETCH(qreal, aspect);
        QVERIFY(m_host->canvas->setProperty("imageSource", fixtureUrl(name)));
        QTRY_VERIFY(m_host->image->isVisible());
        const auto* image = m_host->image.data();
        QVERIFY(image->implicitWidth() > 0 && image->implicitHeight() > 0);
        QVERIFY(qAbs(image->width() / image->height() - aspect) < 0.001);
        QVERIFY(qAbs(image->implicitWidth() / image->implicitHeight() - aspect) < 0.001);
        QVERIFY(image->width() <= m_host->canvas->width());
        QVERIFY(image->height() <= m_host->canvas->height());
        QVERIFY((m_host->markerCenter(0) - m_host->imagePoint(0.5, 0.5)).manhattanLength() <= 1);

        // Neither layout geometry nor the requested sourceSize proves the decode budget.
        // Inspect the actual texture on the render thread, without private Qt APIs.
        const auto textureWidth = std::make_shared<std::atomic<int>>(0);
        const auto textureHeight = std::make_shared<std::atomic<int>>(0);
        const auto connection = connect(
            &m_host->window, &QQuickWindow::afterRendering, &m_host->window,
            [guard = m_host->image, textureWidth, textureHeight] {
                if (!guard)
                    return;
                const auto* provider = guard->textureProvider();
                if (provider && provider->texture()) {
                    const QSize size = provider->texture()->textureSize();
                    textureWidth->store(size.width());
                    textureHeight->store(size.height());
                }
            },
            Qt::DirectConnection);
        const auto disconnect = qScopeGuard([connection] { QObject::disconnect(connection); });
        m_host->window.update();
        QTRY_VERIFY(textureWidth->load() > 0 && textureHeight->load() > 0);
        const QString decoded = QStringLiteral("Decoded texture is %1x%2; expected at most 2048x3072.")
                                    .arg(textureWidth->load())
                                    .arg(textureHeight->load());
        QVERIFY2(textureWidth->load() <= 2048, qPrintable(decoded));
        QVERIFY2(textureHeight->load() <= 3072, qPrintable(decoded));
        QVERIFY(qAbs(qreal(textureWidth->load()) / textureHeight->load() - aspect) < 0.001);
    }

    void markerTap_data()
    {
        QTest::addColumn<bool>("touchInput");
        QTest::addColumn<int>("tool");
        for (int tool = 0; tool <= 2; ++tool) {
            QTest::newRow(qPrintable(QStringLiteral("mouse-tool-%1").arg(tool))) << false << tool;
            QTest::newRow(qPrintable(QStringLiteral("touch-tool-%1").arg(tool))) << true << tool;
        }
    }

    void markerTap()
    {
        QFETCH(bool, touchInput);
        QFETCH(int, tool);
        QVERIFY(m_host->canvas->setProperty("toolMode", tool));
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        QVERIFY(tapped.isValid() && added.isValid());
        tap(m_host->markerCenter(1), touchInput);
        QTRY_COMPARE(tapped.count(), 1);
        QCOMPARE(tapped.first().first().toString(), labelKey(m_host->labels.at(1)));
        QCOMPARE(added.count(), 0);
        QCOMPARE(m_host->canvas->property("selectedLabel").toString(), labelKey(m_host->labels.first()));
        verifyModelUnchanged();
    }

    void addUsesNormalizedCoordinates_data()
    {
        pointerRows();
    }

    void addUsesNormalizedCoordinates()
    {
        QFETCH(bool, touchInput);
        QVERIFY(m_host->canvas->setProperty("toolMode", 1));
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        QVERIFY(added.isValid());
        tap(m_host->imagePoint(0.2, 0.3), touchInput);
        QTRY_COMPARE(added.count(), 1);
        QVERIFY(qAbs(added.first().at(0).toDouble() - 0.2) < 0.005);
        QVERIFY(qAbs(added.first().at(1).toDouble() - 0.3) < 0.005);
        tap({2, 2}, touchInput); // Letterbox is not part of the image.
        QCOMPARE(added.count(), 1);
        verifyModelUnchanged();
    }

    void selectedMoveCommitsOnlyOnRelease_data()
    {
        pointerRows();
    }

    void selectedMoveCommitsOnlyOnRelease()
    {
        QFETCH(bool, touchInput);
        QVERIFY(m_host->canvas->setProperty("toolMode", 2));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        QVERIFY(moved.isValid() && added.isValid());
        const QPoint start = m_host->markerCenter(0);
        const QPoint end = start + QPoint(70, 25);
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        if (touchInput) {
            touch.press(0, start).commit();
            touch.move(0, start + QPoint(30, 10)).commit();
            QTest::qWait(frameDelay);
            touch.move(0, end).commit();
        }
        else {
            QTest::mousePress(&m_host->window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(&m_host->window, start + QPoint(30, 10), frameDelay);
            QTest::mouseMove(&m_host->window, end, frameDelay);
        }
        QTest::qWait(frameDelay);
        QCOMPARE(moved.count(), 0);
        QVERIFY((m_host->markerCenter(0) - end).manhattanLength() <= 2);
        verifyModelUnchanged();
        if (touchInput)
            touch.release(0, end).commit();
        else
            QTest::mouseRelease(&m_host->window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(moved.count(), 1);
        QCOMPARE(moved.first().at(0).toString(), labelKey(m_host->labels.first()));
        QVERIFY(qAbs(moved.first().at(1).toDouble() - (0.5 + 70 / m_host->image->width())) < 0.005);
        QVERIFY(qAbs(moved.first().at(2).toDouble() - (0.5 + 25 / m_host->image->height())) < 0.005);
        QTest::qWait(frameDelay);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(added.count(), 0);
        verifyModelUnchanged();
    }

    void unselectedMarkerDragIsNotAnEdit()
    {
        QVERIFY(m_host->canvas->setProperty("toolMode", 2));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        mouseDrag(m_host->markerCenter(1), {-60, -30});
        QCOMPARE(moved.count(), 0);
        verifyModelUnchanged();
    }

    void contextChangeCancelsMove_data()
    {
        QTest::addColumn<QString>("change");
        for (const auto* change : {"mode", "selection", "resize", "model-reset", "page", "disable", "hide", "reset"})
            QTest::newRow(change) << QString::fromLatin1(change);
    }

    void contextChangeCancelsMove()
    {
        QFETCH(QString, change);
        QVERIFY(m_host->canvas->setProperty("toolMode", 2));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        const QPoint start = m_host->markerCenter(0);
        QTest::mousePress(&m_host->window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&m_host->window, start + QPoint(40, 20), frameDelay);
        if (change == "mode")
            m_host->canvas->setProperty("toolMode", 0);
        else if (change == "selection")
            m_host->canvas->setProperty("selectedLabel", labelKey(m_host->labels.at(1)));
        else if (change == "resize")
            m_host->resize({600, 520});
        else if (change == "model-reset") {
            m_host->model.refresh({}, {}, {});
            m_host->refresh();
        }
        else if (change == "page")
            m_host->canvas->setProperty("imageSource", fixtureUrl("landscape.png"));
        else if (change == "disable")
            m_host->canvas->setEnabled(false);
        else if (change == "hide")
            m_host->canvas->setVisible(false);
        else
            QVERIFY(QMetaObject::invokeMethod(m_host->canvas.get(), "resetView"));
        QTest::mouseRelease(&m_host->window, Qt::LeftButton, Qt::NoModifier, start + QPoint(70, 30));
        QTest::qWait(frameDelay);
        QCOMPARE(moved.count(), 0);
        m_host->canvas->setEnabled(true);
        m_host->canvas->setVisible(true);
        QTRY_VERIFY(m_host->image->isVisible());
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(1), false);
        QTRY_COMPARE(tapped.count(), 1);
        verifyModelUnchanged();
    }

    void secondFingerCancelsAndNextGestureWorks_data()
    {
        QTest::addColumn<bool>("releaseOriginalFirst");
        QTest::newRow("release-second-first") << false;
        QTest::newRow("release-original-first") << true;
    }

    void secondFingerCancelsAndNextGestureWorks()
    {
        QFETCH(bool, releaseOriginalFirst);
        QVERIFY(m_host->canvas->setProperty("toolMode", 2));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        const QPoint start = m_host->markerCenter(0);
        const QPoint end = start + QPoint(60, 20);
        const QPoint second(180, 140);
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        touch.press(0, start).commit();
        touch.move(0, start + QPoint(30, 10)).commit();
        QTest::qWait(frameDelay);
        touch.move(0, end).commit();
        QTest::qWait(frameDelay);
        QVERIFY((m_host->markerCenter(0) - end).manhattanLength() <= 2);
        touch.stationary(0).press(1, second).commit();
        QTest::qWait(frameDelay);
        QVERIFY((m_host->markerCenter(0) - m_host->imagePoint(0.5, 0.5)).manhattanLength() <= 2);
        if (releaseOriginalFirst) {
            touch.release(0, end).stationary(1).commit();
            touch.release(1, second).commit();
        }
        else {
            touch.stationary(0).release(1, second).commit();
            touch.release(0, end).commit();
        }
        QTest::qWait(frameDelay);
        QCOMPARE(moved.count(), 0);

        // No resetView(), model replacement, tool change or fresh component may unstick it.
        const QPoint nextStart = m_host->markerCenter(0);
        touch.press(0, nextStart).commit();
        touch.move(0, nextStart + QPoint(30, 10)).commit();
        QTest::qWait(frameDelay);
        touch.move(0, nextStart + QPoint(60, 20)).commit();
        QTest::qWait(frameDelay);
        touch.release(0, nextStart + QPoint(60, 20)).commit();
        QTRY_COMPARE(moved.count(), 1);
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(1), true);
        QTRY_COMPARE(tapped.count(), 1);
        verifyModelUnchanged();
    }

    void touchCancelDoesNotCommit()
    {
        QVERIFY(m_host->canvas->setProperty("toolMode", 2));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        const QPoint start = m_host->markerCenter(0);
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        touch.press(0, start).commit();
        touch.move(0, start + QPoint(30, 10)).commit();
        QTest::qWait(frameDelay);
        touch.move(0, start + QPoint(60, 20)).commit();
        QTest::qWait(frameDelay);
        QTouchEvent cancel(QEvent::TouchCancel, m_touchDevice);
        QCoreApplication::sendEvent(&m_host->window, &cancel);
        touch.release(0, start + QPoint(60, 20)).commit();
        QTest::qWait(frameDelay);
        QCOMPARE(moved.count(), 0);
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(0), true);
        QTRY_COMPARE(tapped.count(), 1);
    }

    void twoFingerTapDoesNotAddAndNextTapWorks()
    {
        QVERIFY(m_host->canvas->setProperty("toolMode", 1));
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        const QPoint first = m_host->imagePoint(0.2, 0.3);
        const QPoint second = m_host->imagePoint(0.7, 0.3);
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        touch.press(0, first).commit();
        touch.stationary(0).press(1, second).commit();
        QTest::qWait(frameDelay);
        touch.release(0, first).stationary(1).commit();
        touch.release(1, second).commit();
        QTest::qWait(frameDelay);
        QCOMPARE(added.count(), 0);
        tap(first, true);
        QTRY_COMPARE(added.count(), 1);
        verifyModelUnchanged();
    }

    void pinchZoomPanAndFixedMarkers()
    {
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        const qreal fitWidth = m_host->image->width();
        const QSizeF markerSize = m_host->marker(0)->size();
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        touch.press(0, {230, 240}).press(1, {410, 240}).commit();
        QTest::qWait(frameDelay);
        touch.move(0, {200, 240}).move(1, {440, 240}).commit();
        QTest::qWait(frameDelay);
        touch.move(0, {160, 240}).move(1, {480, 240}).commit();
        QTest::qWait(frameDelay);
        touch.release(0, {160, 240}).release(1, {480, 240}).commit();
        QTRY_VERIFY(m_host->image->width() > fitWidth * 1.1);
        QCOMPARE(m_host->marker(0)->size(), markerSize);
        const qreal oldY = m_host->image->y();
        mouseDrag({300, 180}, {0, -50});
        QVERIFY(m_host->image->y() < oldY - 20);
        QCOMPARE(moved.count(), 0);
        QCOMPARE(added.count(), 0);
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(0), false);
        QTRY_COMPARE(tapped.count(), 1);
        QVERIFY(QMetaObject::invokeMethod(m_host->canvas.get(), "resetView"));
        QVERIFY(qAbs(m_host->image->width() - fitWidth) < 0.01);
    }

    void pinchReleasePreservesViewport_data()
    {
        QTest::addColumn<int>("firstReleased");
        QTest::newRow("together") << -1;
        QTest::newRow("left-first") << 0;
        QTest::newRow("right-first") << 1;
    }

    void pinchReleasePreservesViewport()
    {
        QFETCH(int, firstReleased);
        auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
        touch.press(0, {220, 180}).press(1, {400, 200}).commit();
        QTest::qWait(frameDelay);
        touch.move(0, {190, 150}).move(1, {430, 230}).commit();
        QTest::qWait(frameDelay);
        touch.move(0, {140, 120}).move(1, {480, 270}).commit();
        QTest::qWait(frameDelay);
        const QRectF before(m_host->image->position(), m_host->image->size());
        if (firstReleased < 0) {
            touch.release(0, {140, 120}).release(1, {480, 270}).commit();
        }
        else {
            const int remaining = 1 - firstReleased;
            const QPoint first = firstReleased == 0 ? QPoint(140, 120) : QPoint(480, 270);
            const QPoint last = firstReleased == 0 ? QPoint(480, 270) : QPoint(140, 120);
            touch.release(firstReleased, first).stationary(remaining).commit();
            QTest::qWait(frameDelay);
            QCOMPARE(QRectF(m_host->image->position(), m_host->image->size()), before);
            touch.move(remaining, last + QPoint(40, 30)).commit();
            QTest::qWait(frameDelay);
            touch.release(remaining, last + QPoint(40, 30)).commit();
        }
        QTest::qWait(frameDelay);
        QCOMPARE(QRectF(m_host->image->position(), m_host->image->size()), before);
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(0), true);
        QTRY_COMPARE(tapped.count(), 1);
    }

    void markerRenumberingKeepsTapIdentity()
    {
        const auto secondKey = labelKey(m_host->labels[1]);
        m_host->labels[0].setDeleted(true);
        m_host->refresh();
        QTRY_VERIFY(!m_host->marker(0));
        QTRY_VERIFY(m_host->marker(1));
        QCOMPARE(m_host->marker(1)->property("markerNumber").toInt(), 1);
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(1), true);
        QTRY_COMPARE(tapped.count(), 1);
        QCOMPARE(tapped.first().first().toString(), secondKey);
        m_host->labels[0].setDeleted(false);
        m_host->refresh();
        QTRY_VERIFY(m_host->marker(0));
        QTRY_VERIFY(m_host->marker(1));
        QCOMPARE(m_host->marker(1)->property("markerNumber").toInt(), 2);
    }

    void compactBadgeKeepsTouchTarget()
    {
        auto* marker = m_host->marker(0);
        QCOMPARE(marker->size(), QSizeF(48, 48));
        auto* badge = marker->findChild<QQuickItem*>(QStringLiteral("markerBadge"));
        QVERIFY(badge);
        QCOMPARE(badge->size(), QSizeF(22, 22));
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(0) + QPoint(20, 0), true);
        QTRY_COMPARE(tapped.count(), 1);
    }

    void groupColorAndShapeUpdateWithoutReplacingMarker()
    {
        QPointer<QQuickItem> marker = m_host->marker(0);
        auto* badge = marker->findChild<QQuickItem*>(QStringLiteral("markerBadge"));
        QVERIFY(badge);
        const auto styles = labelqt::core::defaultLabelGroupStyles();
        QCOMPARE(badge->property("color").value<QColor>(), styles.at(0).groupColor);
        QCOMPARE(badge->property("radius").toReal(), 11.0);
        m_host->labels[0].setGroup("Other");
        m_host->model.refresh(m_host->labels, {"Text", "Other"}, {"Text", "Other"});
        QVERIFY(marker);
        QCOMPARE(m_host->marker(0), marker.data());
        QCOMPARE(badge->property("color").value<QColor>(), styles.at(1).groupColor);
        QCOMPARE(badge->property("radius").toReal(), 2.0);
        m_host->canvas->setProperty("selectedLabel", QString{});
        QCOMPARE(badge->property("color").value<QColor>(), styles.at(1).groupColor);
    }

    void wheelZoomIsBounded()
    {
        const qreal fitWidth = m_host->image->width();
        const QPointF point(320, 240);
        const QPointF global = m_host->window.mapToGlobal(point.toPoint());
        QWheelEvent zoomIn(point, global, {}, {0, 12000}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&m_host->window, &zoomIn);
        QTest::qWait(frameDelay);
        QVERIFY(m_host->image->width() > fitWidth * 2);
        QVERIFY(m_host->image->width() <= fitWidth * 8.001);
        QWheelEvent zoomOut(point, global, {}, {0, -12000}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&m_host->window, &zoomOut);
        QTest::qWait(frameDelay);
        QVERIFY(qAbs(m_host->image->width() - fitWidth) < 0.01);
    }

    void missingImageDoesNotEmitEdits()
    {
        QVERIFY(m_host->canvas->setProperty("imageSource", QUrl{}));
        QVERIFY(m_host->canvas->setProperty("toolMode", 1));
        QTRY_VERIFY(!m_host->image->isVisible());
        QSignalSpy added(m_host->canvas.get(), SIGNAL(labelAdded(double, double)));
        QSignalSpy moved(m_host->canvas.get(), SIGNAL(labelMoved(QString, double, double)));
        tap({200, 200}, false);
        tap({200, 200}, true);
        QCOMPARE(added.count(), 0);
        QCOMPARE(moved.count(), 0);
    }

    void renderPhoneAndTablet_data()
    {
        QTest::addColumn<QSize>("viewport");
        QTest::addColumn<QString>("artifact");
        QTest::newRow("phone") << QSize(360, 640) << QStringLiteral("mobile-canvas-phone.png");
        QTest::newRow("tablet") << QSize(1024, 768) << QStringLiteral("mobile-canvas-tablet.png");
    }

    void renderPhoneAndTablet()
    {
        QFETCH(QSize, viewport);
        QFETCH(QString, artifact);
        m_host->resize(viewport);
        QVERIFY(QMetaObject::invokeMethod(m_host->canvas.get(), "resetView"));
        QImage capture;
        QTRY_VERIFY(!(capture = m_host->window.grabWindow()).isNull());
        const qreal scaleX = qreal(capture.width()) / viewport.width();
        const qreal scaleY = qreal(capture.height()) / viewport.height();
        QVERIFY(scaleX >= 1 && qAbs(scaleX - scaleY) < 0.01);
        const auto pixel = [&](QPoint position) {
            return capture.pixelColor(qRound(position.x() * scaleX), qRound(position.y() * scaleY));
        };
        // Pixel checks ensure the decoded image and both marker styles actually rendered.
        QVERIFY(nearColor(pixel(m_host->imagePoint(0.2, 0.2)), topLeftColor));
        QVERIFY(nearColor(pixel(m_host->imagePoint(0.8, 0.2)), topRightColor));
        const auto groupColor = labelqt::core::defaultLabelGroupStyles().first().groupColor;
        QVERIFY(nearColor(pixel(m_host->markerCenter(0) - QPoint(7, 0)), groupColor));
        QVERIFY(nearColor(pixel(m_host->markerCenter(1) - QPoint(7, 0)), groupColor));
        const QString output = qEnvironmentVariable("LABELQT_TEST_SCREENSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(capture.save(QDir(output).filePath(artifact)));
        }
        QSignalSpy tapped(m_host->canvas.get(), SIGNAL(labelTapped(QString)));
        tap(m_host->markerCenter(0), true);
        QTRY_COMPARE(tapped.count(), 1);
    }

private:
    QUrl fixtureUrl(const QString& name) const
    {
        return QUrl::fromLocalFile(m_directory.filePath(name));
    }

    void tap(QPoint position, bool touchInput)
    {
        if (touchInput) {
            auto touch = QTest::touchEvent(&m_host->window, m_touchDevice, false);
            touch.press(0, position).commit();
            QTest::qWait(frameDelay);
            touch.release(0, position).commit();
        }
        else {
            QTest::mouseClick(&m_host->window, Qt::LeftButton, Qt::NoModifier, position);
        }
        QTest::qWait(frameDelay);
    }

    void mouseDrag(QPoint start, QPoint delta)
    {
        QTest::mousePress(&m_host->window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&m_host->window, start + delta / 2, frameDelay);
        QTest::mouseMove(&m_host->window, start + delta, frameDelay);
        QTest::mouseRelease(&m_host->window, Qt::LeftButton, Qt::NoModifier, start + delta);
        QTest::qWait(frameDelay);
    }

    void verifyModelUnchanged()
    {
        QCOMPARE(m_host->model.rowCount(), m_host->labels.size());
        for (int row = 0; row < m_host->model.rowCount(); ++row) {
            const auto index = m_host->model.index(row);
            QCOMPARE(index.data(MobileLabelModel::Key).toString(), labelKey(m_host->labels.at(row)));
            QCOMPARE(index.data(MobileLabelModel::PositionX).toDouble(), m_host->labels.at(row).position().x());
            QCOMPARE(index.data(MobileLabelModel::PositionY).toDouble(), m_host->labels.at(row).position().y());
        }
    }

    QTemporaryDir m_directory;
    QString m_qmlPath;
    QStringList m_warnings;
    QPointer<QPointingDevice> m_touchDevice;
    std::unique_ptr<CanvasHost> m_host;
};

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    MobileCanvasTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "MobileCanvasTests.moc"
