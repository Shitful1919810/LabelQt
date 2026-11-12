#include "core/AppPreferences.h"
#include "core/LabelPlusDocument.h"
#include "services/io/ProjectFileTransaction.h"
#include "ui/EditorStateController.h"
#include "ui/MainWindow.h"
#include "ui/MainWindowShortcutController.h"
#include "ui/canvas/CanvasLabelItems.h"
#include "ui/canvas/ImagePageViewController.h"
#include "ui/labeltable/LabelSelectionController.h"
#include "ui/labeltable/LabelTableView.h"
#include "ui/project/PageSelectorComboBox.h"
#include "ui/proofread/ProofreadChangeTableModel.h"
#include "ui/proofread/ProofreadingController.h"
#include "ui/proofread/ReviewStatusDelegate.h"
#include <QAbstractItemModelTester>
#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <memory>

class UiInteractionTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;

    static QString createProject(const QTemporaryDir& dir)
    {
        labelqt::core::Project project;
        project.setGroups({"A", "B"});
        for (int page = 0; page < 2; ++page) {
            const QString name = QStringLiteral("%1.png").arg(page);
            QImage image(100, 100, QImage::Format_RGB32);
            image.fill(Qt::white);
            if (!image.save(dir.filePath(name)))
                return {};
            project.images().append({name,
                                     dir.filePath(name),
                                     {labelqt::core::Label(QStringLiteral("page-%1").arg(page), "A", {0.5, 0.5})}});
        }
        const QString path = dir.filePath("project.txt");
        labelqt::core::LabelPlusDocument::saveToFile(project, path);
        return path;
    }

    static QPlainTextEdit* editFirstRow(MainWindow& window)
    {
        auto* table = window.findChild<LabelTableView*>();
        if (!table)
            return nullptr;
        const auto index = table->model()->index(0, LabelTableModel::TextColumn);
        table->setCurrentIndex(index);
        table->edit(index);
        return table->findChild<QPlainTextEdit*>();
    }

    static QAction* actionForShortcut(MainWindow& window, QKeySequence::StandardKey key)
    {
        const auto shortcut =
            key == QKeySequence::Redo ? labelqt::core::AppPreferences{}.redoShortcut() : QKeySequence(key);
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == shortcut)
                return action;
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("LabelQtUiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    void init()
    {
        QSettings().clear();
    }

    void tableCommitPrecedesPageShortcut_data()
    {
        QTest::addColumn<bool>("labelNavigation");
        QTest::newRow("next-page") << false;
        QTest::newRow("next-label-across-pages") << true;
    }

    void tableCommitPrecedesPageShortcut()
    {
        QFETCH(bool, labelNavigation);
        QTemporaryDir dir;
        const QString path = createProject(dir);
        QVERIFY(!path.isEmpty());
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        window.show();
        window.activateWindow();
        auto* editor = editFirstRow(window);
        QVERIFY(editor);
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), editor);
        editor->setPlainText("edited-first-page");
        if (labelNavigation)
            QTest::keyClick(editor, Qt::Key_Tab);
        else
            QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
        auto* model = window.findChild<LabelTableModel*>();
        QVERIFY(model);
        QCoreApplication::processEvents();
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "page-1");
        const auto combos = window.findChildren<PageSelectorComboBox*>();
        QVERIFY(!combos.isEmpty());
        // Label navigation reopens an editor; commit it before programmatic page selection.
        auto* editorState = window.findChild<EditorStateController*>();
        QVERIFY(editorState);
        editorState->commitActive();
        combos.first()->setCurrentIndex(0);
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "edited-first-page");
    }

    void tableCommitPrecedesSave()
    {
        QTemporaryDir dir;
        const QString path = createProject(dir);
        QVERIFY(!path.isEmpty());
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        window.show();
        window.activateWindow();
        auto* editor = editFirstRow(window);
        QVERIFY(editor);
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), editor);
        editor->setPlainText("saved-editor-text");
        auto* save = actionForShortcut(window, QKeySequence::Save);
        QVERIFY(save);
        save->trigger();
        const auto saved = labelqt::core::LabelPlusDocument::loadFromFile(path);
        QCOMPARE(saved.images().first().labels.first().text(), "saved-editor-text");
        QCoreApplication::processEvents();
        auto* undo = actionForShortcut(window, QKeySequence::Undo);
        auto* redo = actionForShortcut(window, QKeySequence::Redo);
        QVERIFY(undo);
        QVERIFY(redo);
        undo->trigger();
        auto* model = window.findChild<LabelTableModel*>();
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "page-0");
        redo->trigger();
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "saved-editor-text");
    }

    void deferredTableRefreshDoesNotSelectReloadedDocument()
    {
        QTemporaryDir dir;
        const QString path = createProject(dir);
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        auto* table = window.findChild<LabelTableView*>();
        auto* model = window.findChild<LabelTableModel*>();
        QVERIFY(table);
        QVERIFY(model);
        QVERIFY(model->setData(model->index(0, LabelTableModel::TextColumn), "old-session-edit"));
        // Reload keeps all persisted identities, but starts a different document session.
        QVERIFY(window.openProjectFile(path));
        QCoreApplication::processEvents();
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "page-0");
        QVERIFY(!table->selectionModel()->hasSelection());
    }

    void deferredLabelNavigationDoesNotEnterNewDocument()
    {
        QTemporaryDir firstDir, secondDir;
        const auto first = createProject(firstDir);
        const auto second = createProject(secondDir);
        MainWindow window;
        QVERIFY(window.openProjectFile(first));
        window.show();
        window.activateWindow();
        auto* editor = editFirstRow(window);
        QVERIFY(editor);
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), editor);
        editor->setPlainText("old-document-edit");
        QTest::keyClick(editor, Qt::Key_Tab);
        QVERIFY(window.openProjectFile(second));
        QCoreApplication::processEvents();
        auto* table = window.findChild<LabelTableView*>();
        auto* model = window.findChild<LabelTableModel*>();
        QVERIFY(table && model);
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "page-0");
        QVERIFY(!table->selectionModel()->hasSelection());
        for (auto* input : table->findChildren<QPlainTextEdit*>())
            QVERIFY(!input->isVisible());
        table->setCurrentIndex(model->index(0, 0));
        QVERIFY(table->selectionModel()->hasSelection());
    }

    void committedEditorDoesNotOverrideMarkerSelection_data()
    {
        QTest::addColumn<bool>("rightClick");
        QTest::newRow("revision-right-click-preserves-multiselect") << true;
        QTest::newRow("revision-ctrl-click-clears-selection") << false;
    }

    void committedEditorDoesNotOverrideMarkerSelection()
    {
        QFETCH(bool, rightClick);
        QTemporaryDir dir;
        const auto path = createProject(dir);
        auto project = labelqt::core::LabelPlusDocument::loadFromFile(path);
        project.images()[0].labels[0].setPosition({0.2, 0.5});
        project.images()[0].labels.append({"second", "A", {0.5, 0.5}});
        project.images()[0].labels.append({"third", "A", {0.8, 0.5}});
        labelqt::core::LabelPlusDocument::saveToFile(project, path);
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        window.show();
        window.activateWindow();
        auto* canvas = window.findChild<ImageCanvas*>();
        auto* table = window.findChild<LabelTableView*>();
        auto* model = window.findChild<LabelTableModel*>();
        auto* proofreading = window.findChild<ProofreadingController*>();
        QVERIFY(canvas && table && model && proofreading);
        proofreading->toggleBaseline();
        QVERIFY(proofreading->isActive());
        const auto hasMarkers = [&] {
            for (auto* item : canvas->scene()->items())
                if (dynamic_cast<CanvasLabelMarkerItem*>(item))
                    return true;
            return false;
        };
        QTRY_VERIFY(hasMarkers());
        auto* editor = editFirstRow(window);
        QVERIFY(editor);
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), editor);
        if (rightClick)
            table->selectionModel()->select(model->index(1, 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        canvas->restoreView(150, {0.5, 0.5});
        QTest::qWait(200);
        editor->setPlainText("marker-commit");
        const QPoint marker = canvas->viewport()->mapFromGlobal(canvas->globalPositionForLabel(rightClick ? 2 : 0));
        QTest::mouseClick(canvas->viewport(), rightClick ? Qt::RightButton : Qt::LeftButton,
                          rightClick ? Qt::NoModifier : Qt::ControlModifier, marker);
        const auto selectedBeforeDelivery = table->selectionModel()->selectedRows();
        QCOMPARE(selectedBeforeDelivery.size(), rightClick ? 2 : 0);
        const QPointer<QWidget> focus = QApplication::focusWidget();
        const auto center = canvas->normalizedViewCenter();
        QCoreApplication::processEvents();
        QCOMPARE(table->selectionModel()->selectedRows(), selectedBeforeDelivery);
        QCOMPARE(QApplication::focusWidget(), focus.data());
        QCOMPARE(canvas->normalizedViewCenter(), center);
        QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "marker-commit");
        QCOMPARE(model->rowCount(), rightClick ? 2 : 3);
    }

    void oldTableCommitKeepsNewEditorDraftAndPreview()
    {
        QTemporaryDir dir;
        const auto path = createProject(dir);
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        window.show();
        window.activateWindow();
        auto* table = window.findChild<LabelTableView*>();
        auto* model = window.findChild<LabelTableModel*>();
        auto* canvas = window.findChild<ImageCanvas*>();
        auto* state = window.findChild<EditorStateController*>();
        QVERIFY(table && model && canvas && state);
        auto* oldEditor = editFirstRow(window);
        QVERIFY(oldEditor);
        oldEditor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), oldEditor);
        const auto bubbleWidth = [&] {
            for (auto* item : canvas->scene()->items())
                if (auto* bubble = dynamic_cast<CanvasLabelTextBubbleItem*>(item))
                    return bubble->boundingRect().width();
            return 0.0;
        };
        QTRY_VERIFY(bubbleWidth() > 0);
        oldEditor->setPlainText("committed");
        state->commitActive();
        const auto index = model->index(0, LabelTableModel::TextColumn);
        table->setCurrentIndex(index);
        table->edit(index);
        QPointer<QPlainTextEdit> newEditor = qobject_cast<QPlainTextEdit*>(table->indexWidget(index));
        QVERIFY(newEditor && newEditor != oldEditor);
        newEditor->setFocus();
        newEditor->setPlainText("new uncommitted draft with a visibly wider preview");
        const auto width = bubbleWidth();
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(newEditor);
        QCOMPARE(newEditor->toPlainText(), "new uncommitted draft with a visibly wider preview");
        QCOMPARE(bubbleWidth(), width);
        QCOMPARE(QApplication::focusWidget(), newEditor.data());
        QCOMPARE(index.data().toString(), "committed");
        state->commitActive();
    }

    void singleSelectionRejectsHiddenLabel()
    {
        QVector<labelqt::core::Label> labels{{"hidden", "B", {0.5, 0.5}}};
        LabelTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setLabelsProvider([&] { return &labels; });
        model.setGroupFilter({"A"});
        QTableView table;
        table.setModel(&model);
        ImageCanvas canvas;
        LabelSelectionController selection;
        selection.setWidgets(&model, &table, &canvas);
        int details = 0, clears = 0;
        selection.setCallbacks(
            [&](int) {
                ++details;
                return true;
            },
            [&] { ++clears; }, {}, {});
        QVERIFY(!selection.selectSingle(0));
        QCOMPARE(details, 0);
        QCOMPARE(clears, 1);
        QVERIFY(selection.selectedLabelIndexes().isEmpty());
    }

    void changingGroupToHiddenDisablesEditor()
    {
        QTemporaryDir dir;
        const QString path = createProject(dir);
        MainWindow window;
        QVERIFY(window.openProjectFile(path));
        auto* filter = window.findChild<GroupFilterComboBox*>();
        auto* table = window.findChild<LabelTableView*>();
        auto* model = window.findChild<LabelTableModel*>();
        QVERIFY(filter);
        QVERIFY(table);
        QVERIFY(model);
        filter->setSelectedGroups({"A"});
        table->setCurrentIndex(model->index(0, LabelTableModel::NumberColumn));
        QPlainTextEdit* bottom = nullptr;
        for (auto* editor : window.findChildren<QPlainTextEdit*>())
            if (editor->toPlainText() == "page-0")
                bottom = editor;
        QVERIFY(bottom);
        QVERIFY(bottom->isEnabled());
        QVERIFY(model->setData(model->index(0, LabelTableModel::GroupColumn), "B"));
        QTRY_COMPARE(model->rowCount(), 0);
        QVERIFY(!bottom->isEnabled());
        QVERIFY(bottom->toPlainText().isEmpty());
        QVERIFY(!table->selectionModel()->hasSelection());
        auto* undo = actionForShortcut(window, QKeySequence::Undo);
        QVERIFY(undo);
        undo->trigger();
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(bottom->isEnabled());
    }

    void recentProjectSaveWorkflow_data()
    {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("conflict-save-copy") << QStringLiteral("copy");
        QTest::newRow("cancel-unsaved") << QStringLiteral("cancel");
        QTest::newRow("save-failure") << QStringLiteral("failure");
    }

    void recentProjectSaveWorkflow()
    {
        QFETCH(QString, scenario);
        QTemporaryDir firstDir, secondDir;
        const QString first = createProject(firstDir);
        const QString second = createProject(secondDir);
        const QString copy = firstDir.filePath("copy.txt");
        MainWindow window;
        QVERIFY(window.openProjectFile(second));
        QVERIFY(window.openProjectFile(first));
        auto* model = window.findChild<LabelTableModel*>();
        QVERIFY(model);
        QVERIFY(model->setData(model->index(0, LabelTableModel::TextColumn), "local-edit"));
        if (scenario == "copy") {
            QFile file(first);
            QVERIFY(file.open(QIODevice::Append));
            QCOMPARE(file.write("\n"), 1);
        }
        std::unique_ptr<QLockFile> lock;
        if (scenario == "failure") {
            lock = std::make_unique<QLockFile>(labelqt::services::ProjectFileTransaction::lockPath(first));
            QVERIFY(lock->tryLock());
        }
        QPointer<QAction> recent;
        for (auto* action : window.findChildren<QAction*>())
            if (action->data().toString() == second)
                recent = action;
        QVERIFY(recent);
        QElapsedTimer elapsed;
        elapsed.start();
        int prompts = 0;
        bool finished = false, unexpected = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (elapsed.hasExpired(5000)) {
                unexpected = true;
                dialog->reject();
                return;
            }
            if (auto* box = qobject_cast<QMessageBox*>(dialog)) {
                ++prompts;
                if (box->standardButtons().testFlag(QMessageBox::Save)) {
                    const auto choice = scenario == "cancel" ? QMessageBox::Cancel : QMessageBox::Save;
                    finished = scenario == "cancel";
                    box->button(choice)->click();
                }
                else if (scenario == "copy") {
                    for (auto* button : box->buttons()) {
                        if (button->text() == "Save current edits as...") {
                            button->click();
                            return;
                        }
                    }
                    unexpected = true;
                    box->reject();
                }
                else if (scenario == "failure" && box->icon() == QMessageBox::Critical) {
                    finished = true;
                    box->accept();
                }
                else {
                    unexpected = true;
                    box->reject();
                }
            }
            else if (auto* file = qobject_cast<QFileDialog*>(dialog); file && scenario == "copy") {
                file->selectFile(copy);
                QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
                finished = true;
            }
            else {
                unexpected = true;
                dialog->reject();
            }
        });
        responder.start(5);
        recent->trigger();
        // No modal workflow or menu destruction is permitted in the trigger stack.
        QCOMPARE(prompts, 0);
        QVERIFY(recent);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 6000);
        QVERIFY(!unexpected);
        if (scenario == "copy") {
            QTRY_VERIFY(window.windowTitle().contains(second));
            QVERIFY(recent.isNull());
            QCOMPARE(labelqt::core::LabelPlusDocument::loadFromFile(copy).images().first().labels.first().text(),
                     "local-edit");
        }
        else {
            QVERIFY(window.windowTitle().contains(first));
            QVERIFY(window.windowTitle().endsWith(" *"));
            QCOMPARE(model->index(0, LabelTableModel::TextColumn).data().toString(), "local-edit");
            QCOMPARE(labelqt::core::LabelPlusDocument::loadFromFile(first).images().first().labels.first().text(),
                     "page-0");
        }
    }

    void imageRestoreBelongsToDisplayRequest_data()
    {
        QTest::addColumn<bool>("cached");
        QTest::addColumn<bool>("restoreNewTarget");
        QTest::newRow("async-discard-old") << false << false;
        QTest::newRow("cached-discard-old") << true << false;
        QTest::newRow("async-restore-new") << false << true;
        QTest::newRow("cached-restore-new") << true << true;
    }

    void imageRestoreBelongsToDisplayRequest()
    {
        QFETCH(bool, cached);
        QFETCH(bool, restoreNewTarget);
        QTemporaryDir dir;
        const auto path = createProject(dir);
        auto project = labelqt::core::LabelPlusDocument::loadFromFile(path);
        const auto first = project.images().first();
        const auto second = project.images().last();
        project.images() = {first};
        const auto gate = std::make_shared<QSemaphore>();
        labelqt::services::ImagePageCache cache([gate, first](const QString& imagePath, QSize size) {
            if (imagePath == first.path)
                gate->tryAcquire(1, 5000);
            labelqt::services::ImagePageLoadResult result;
            result.path = imagePath;
            result.requestedPreviewSize = size;
            result.sourceRevision = *labelqt::services::FileRevision::read(imagePath);
            result.image = QImage(imagePath);
            return result;
        });
        const auto cleanup = qScopeGuard([&] {
            gate->release(10);
            QThreadPool::globalInstance()->waitForDone();
        });
        ImageCanvas canvas;
        canvas.resize(400, 300);
        ImagePageViewController controller;
        controller.setCanvas(&canvas);
        controller.setCache(&cache);
        controller.setProjectProvider([&]() -> const labelqt::core::Project& { return project; });
        controller.setCurrentImageIndexProvider([] { return 0; });
        QSignalSpy restored(&controller, &ImagePageViewController::viewRestored);
        QSignalSpy loaded(&cache, &labelqt::services::ImagePageCache::imageLoaded);
        if (cached) {
            cache.requestImage(second.path, {});
            QTRY_VERIFY(cache.cachedImage(second.path, {}).has_value());
        }
        controller.displayCurrentImage();
        controller.restoreViewAfterCurrentImageDisplayed(250, {0.2, 0.3});
        project.images() = {second};
        controller.displayCurrentImage();
        if (restoreNewTarget)
            controller.restoreViewAfterCurrentImageDisplayed(175, {0.6, 0.7});
        QTRY_VERIFY(cache.cachedImage(second.path, {}).has_value());
        QCOMPARE(restored.count(), restoreNewTarget ? 1 : 0);
        QCOMPARE(canvas.zoomPercent(), restoreNewTarget ? 175 : 100);
        const auto delivered = loaded.count();
        gate->release();
        QTRY_VERIFY(loaded.count() > delivered);
        QCOMPARE(restored.count(), restoreNewTarget ? 1 : 0);
        QCOMPARE(canvas.zoomPercent(), restoreNewTarget ? 175 : 100);
    }

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
