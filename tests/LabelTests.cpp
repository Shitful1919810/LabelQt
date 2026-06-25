#include "core/AppPreferences.h"
#include "core/Label.h"
#include "core/LabelPlusDocument.h"
#include "core/Project.h"
#include "services/AutomationOperationApplier.h"
#include "services/LabelNavigator.h"
#include "services/PageSourceInfoService.h"
#include "services/ProjectImageValidator.h"
#include "services/ProjectMergeService.h"
#include "services/ProjectPageOrderService.h"
#include "services/SessionStateStore.h"
#include "ui/ImageCanvas.h"
#include "ui/LabelTableModel.h"
#include "ui/PageOrderListModel.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QTextStream>
#include <QUuid>
#include <QtTest/QtTest>

#include <memory>

using labelqt::core::Label;
using labelqt::core::LabelPlusDocument;

class LabelTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("LabelQtTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LabelQtTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::temp().filePath(QStringLiteral("labelqt_test_settings")));
    }

    void positionIsClamped()
    {
        Label label("text", "group", QPointF(-1.0, 2.0));

        QCOMPARE(label.position().x(), 0.0);
        QCOMPARE(label.position().y(), 1.0);
    }

    void labelPlusDocumentRoundTrips()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_parser_test");
        QDir().mkpath(dirPath);
        const QString filePath = QDir(dirPath).filePath("translation.txt");

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "1,0\n-\n框内\n框外\n-\n\n"
               << ">>>>>>>>[01.png]<<<<<<<<\n"
               << "----------------[1]----------------[0.170,0.344,2]\n"
               << "题目：憧憬的回忆\n\n";
        file.close();

        auto project = LabelPlusDocument::loadFromFile(filePath);

        QCOMPARE(project.groups().size(), 2);
        QCOMPARE(project.images().size(), 1);
        QCOMPARE(project.images().first().labels.size(), 1);
        QCOMPARE(project.images().first().labels.first().group(), QStringLiteral("框外"));

        LabelPlusDocument::saveToFile(project, filePath);
        auto reloaded = LabelPlusDocument::loadFromFile(filePath);
        QCOMPARE(reloaded.images().first().labels.first().text(), QStringLiteral("题目：憧憬的回忆"));
    }

    void labelNavigatorMovesAcrossVisibleLabels()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("a"), QStringLiteral("框内"), {}));
        project.images().last().labels.append(Label(QStringLiteral("b"), QStringLiteral("框外"), {}));
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("c"), QStringLiteral("框内"), {}));
        project.images().last().labels.append(Label(QStringLiteral("d"), QStringLiteral("框外"), {}));

        const QStringList visibleGroups{QStringLiteral("框内"), QStringLiteral("框外")};
        const auto next = labelqt::services::LabelNavigator::nextVisibleLabel(project, {0, 1, visibleGroups});
        QVERIFY(next.isValid());
        QCOMPARE(next.imageIndex, 1);
        QCOMPARE(next.labelIndex, 0);

        const auto previous = labelqt::services::LabelNavigator::previousVisibleLabel(project, {1, 0, visibleGroups});
        QVERIFY(previous.isValid());
        QCOMPARE(previous.imageIndex, 0);
        QCOMPARE(previous.labelIndex, 1);
    }

    void labelNavigatorRespectsVisibleGroups()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("a"), QStringLiteral("框外"), {}));
        project.images().last().labels.append(Label(QStringLiteral("b"), QStringLiteral("框内"), {}));
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("c"), QStringLiteral("框外"), {}));
        project.images().last().labels.append(Label(QStringLiteral("d"), QStringLiteral("框内"), {}));

        const QStringList visibleGroups{QStringLiteral("框内")};
        const auto next = labelqt::services::LabelNavigator::nextVisibleLabel(project, {0, 1, visibleGroups});
        QVERIFY(next.isValid());
        QCOMPARE(next.imageIndex, 1);
        QCOMPARE(next.labelIndex, 1);

        const auto previous = labelqt::services::LabelNavigator::previousVisibleLabel(project, {1, 1, visibleGroups});
        QVERIFY(previous.isValid());
        QCOMPARE(previous.imageIndex, 0);
        QCOMPARE(previous.labelIndex, 1);
    }

    void appPreferencesLoadsAutomationShortcuts()
    {
        const QByteArray json = R"({
            "automation": {
                "shortcuts": {
                    "official:test:word_count": "Ctrl+Alt+W",
                    "official:test:bad": 12,
                    "": "Ctrl+Alt+E"
                }
            }
        })";

        const labelqt::core::AppPreferencesLoadResult result = labelqt::core::AppPreferences::loadFromJson(json);

        QCOMPARE(result.preferences.automationShortcuts().size(), 1);
        QCOMPARE(result.preferences.automationShortcuts().value(QStringLiteral("official:test:word_count")),
                 QKeySequence(QStringLiteral("Ctrl+Alt+W")));
        QCOMPARE(result.warnings.size(), 2);
    }

    void automationOperationApplierAddsLabelsWithUndo()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});

        labelqt::services::AutomationOperation operation;
        operation.type = QStringLiteral("addLabel");
        operation.page = QStringLiteral("001.png");
        operation.group = QStringLiteral("框内");
        operation.text = QStringLiteral("识别文本");
        operation.x = 0.75;
        operation.y = 0.25;

        const auto plan = labelqt::services::AutomationOperationApplier::plan(project, {operation});
        QVERIFY(plan.hasChanges());
        QCOMPARE(plan.changeCount(), 1);

        labelqt::services::AutomationOperationApplier::apply(project, plan, true);
        QCOMPARE(project.images().first().labels.size(), 1);
        QCOMPARE(project.images().first().labels.first().text(), QStringLiteral("识别文本"));
        QCOMPARE(project.images().first().labels.first().group(), QStringLiteral("框内"));
        QCOMPARE(project.images().first().labels.first().position().x(), 0.75);
        QCOMPARE(project.images().first().labels.first().position().y(), 0.25);

        labelqt::services::AutomationOperationApplier::apply(project, plan, false);
        QCOMPARE(project.images().first().labels.size(), 0);

        labelqt::services::AutomationOperationApplier::apply(project, plan, true);
        QCOMPARE(project.images().first().labels.size(), 1);
        QCOMPARE(project.images().first().labels.first().text(), QStringLiteral("识别文本"));
    }

    void automationOperationApplierEditsAndDeletesLabelsWithUndo()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("原文本"), QStringLiteral("框内"), {0.2, 0.3}));
        project.images().last().labels.append(Label(QStringLiteral("删除我"), QStringLiteral("框外"), {0.4, 0.5}));

        labelqt::services::AutomationOperation textOperation;
        textOperation.type = QStringLiteral("setLabelText");
        textOperation.page = QStringLiteral("001.png");
        textOperation.labelIndex = 0;
        textOperation.text = QStringLiteral("新文本");

        labelqt::services::AutomationOperation positionOperation;
        positionOperation.type = QStringLiteral("setLabelPosition");
        positionOperation.page = QStringLiteral("001.png");
        positionOperation.labelIndex = 0;
        positionOperation.x = 0.8;
        positionOperation.y = 0.9;

        labelqt::services::AutomationOperation deleteOperation;
        deleteOperation.type = QStringLiteral("deleteLabel");
        deleteOperation.page = QStringLiteral("001.png");
        deleteOperation.labelIndex = 1;

        const auto plan = labelqt::services::AutomationOperationApplier::plan(
            project, {textOperation, positionOperation, deleteOperation});
        QVERIFY(plan.hasChanges());
        QCOMPARE(plan.changeCount(), 3);

        labelqt::services::AutomationOperationApplier::apply(project, plan, true);
        QCOMPARE(project.images().first().labels.at(0).text(), QStringLiteral("新文本"));
        QCOMPARE(project.images().first().labels.at(0).position().x(), 0.8);
        QCOMPARE(project.images().first().labels.at(0).position().y(), 0.9);
        QVERIFY(project.images().first().labels.at(1).isDeleted());

        labelqt::services::AutomationOperationApplier::apply(project, plan, false);
        QCOMPARE(project.images().first().labels.at(0).text(), QStringLiteral("原文本"));
        QCOMPARE(project.images().first().labels.at(0).position().x(), 0.2);
        QCOMPARE(project.images().first().labels.at(0).position().y(), 0.3);
        QVERIFY(!project.images().first().labels.at(1).isDeleted());
    }

    void labelTableModelPassesModelTesterAndFiltersGroups()
    {
        QVector<Label> labels{
            Label(QStringLiteral("inside"), QStringLiteral("框内"), {0.2, 0.3}),
            Label(QStringLiteral("outside"), QStringLiteral("框外"), {0.4, 0.5}),
            Label(QStringLiteral("deleted"), QStringLiteral("框内"), {0.6, 0.7}),
        };
        labels[2].setDeleted(true);

        LabelTableModel model;
        const QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        model.setGroups({QStringLiteral("框内"), QStringLiteral("框外")}, {});
        model.setLabels(&labels);
        model.setGroupFilter({QStringLiteral("框内"), QStringLiteral("框外")});

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, LabelTableModel::NumberColumn)).toInt(), 1);
        QCOMPARE(model.data(model.index(1, LabelTableModel::NumberColumn)).toInt(), 2);
        QCOMPARE(model.sourceIndexForRow(0), 0);
        QCOMPARE(model.sourceIndexForRow(1), 1);

        model.setGroupFilter({QStringLiteral("框外")});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, LabelTableModel::NumberColumn)).toInt(), 1);
        QCOMPARE(model.sourceIndexForRow(0), 1);
        QCOMPARE(model.rowForSourceIndex(0), -1);
        QCOMPARE(model.rowForSourceIndex(1), 0);
    }

    void pageOrderListModelPassesModelTesterAndKeepsPagesAfterDragDrop()
    {
        labelqt::core::Project project;
        for (int i = 0; i < 6; ++i) {
            project.images().append(
                labelqt::core::ImageEntry{QStringLiteral("%1.png").arg(i, 3, 10, QLatin1Char('0')), {}, {}});
        }

        PageOrderListModel model(project);
        const QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        std::unique_ptr<QMimeData> firstMove(model.mimeData({model.index(1, PageOrderListModel::ImageNameColumn),
                                                             model.index(2, PageOrderListModel::ImageNameColumn)}));
        QVERIFY(firstMove != nullptr);
        QVERIFY(model.dropMimeData(firstMove.get(), Qt::MoveAction, 5, 0, {}));
        QCOMPARE(model.pageOrder(), QVector<int>({0, 3, 4, 1, 2, 5}));
        QCOMPARE(model.lastMovedSourceIndexes(), QVector<int>({1, 2}));

        std::unique_ptr<QMimeData> secondMove(model.mimeData({model.index(3, PageOrderListModel::ImageNameColumn),
                                                              model.index(4, PageOrderListModel::ImageNameColumn)}));
        QVERIFY(secondMove != nullptr);
        QVERIFY(model.dropMimeData(secondMove.get(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(model.pageOrder(), QVector<int>({1, 2, 0, 3, 4, 5}));

        QVector<int> sortedOrder = model.pageOrder();
        std::sort(sortedOrder.begin(), sortedOrder.end());
        QCOMPARE(sortedOrder, QVector<int>({0, 1, 2, 3, 4, 5}));
    }

    void sessionStateStorePersistsMultiSelectedLabels()
    {
        const QString projectPath =
            QDir::temp().filePath(QStringLiteral("labelqt_session_%1.txt").arg(QUuid::createUuid().toString()));
        QFile projectFile(projectPath);
        QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Text));
        projectFile.close();

        labelqt::services::ProjectSessionState state;
        state.isValid = true;
        state.imageName = QStringLiteral("012.png");
        state.imageIndex = 6;
        state.zoomPercent = 175;
        state.viewCenter = QPointF(0.25, 0.75);
        state.selectedLabelIndex = 5;
        state.selectedLabelIndexes = {1, 5, 8};

        labelqt::services::SessionStateStore store;
        store.saveProjectSession(projectPath, state);
        const labelqt::services::ProjectSessionState loaded = store.loadProjectSession(projectPath);

        QVERIFY(loaded.isValid);
        QCOMPARE(loaded.imageName, QStringLiteral("012.png"));
        QCOMPARE(loaded.imageIndex, 6);
        QCOMPARE(loaded.zoomPercent, 175);
        QCOMPARE(loaded.viewCenter, QPointF(0.25, 0.75));
        QCOMPARE(loaded.selectedLabelIndex, 5);
        QCOMPARE(loaded.selectedLabelIndexes, QVector<int>({1, 5, 8}));
    }

    void projectImageValidatorFindsMissingImages()
    {
        const QString dirPath = QDir::temp().filePath(QStringLiteral("labelqt_missing_images_test"));
        QDir().mkpath(dirPath);

        const QString existingPath = QDir(dirPath).filePath(QStringLiteral("001.png"));
        QFile existingFile(existingPath);
        QVERIFY(existingFile.open(QIODevice::WriteOnly));
        existingFile.close();

        labelqt::core::Project project;
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), existingPath, {}});
        project.images().append(labelqt::core::ImageEntry{
            QStringLiteral("002.png"), QDir(dirPath).filePath(QStringLiteral("002.png")), {}});

        const QVector<labelqt::services::MissingProjectImage> missingImages =
            labelqt::services::ProjectImageValidator::missingImages(project);

        QCOMPARE(missingImages.size(), 1);
        QCOMPARE(missingImages.first().imageIndex, 1);
        QCOMPARE(missingImages.first().imageName, QStringLiteral("002.png"));
        QCOMPARE(missingImages.first().imagePath, QDir(dirPath).filePath(QStringLiteral("002.png")));
    }

    void imageCanvasCtrlClickMarkerEmitsMultiSelectClickWhenCtrlMovesMarker()
    {
        ImageCanvas canvas;
        canvas.resize(640, 480);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QVector<Label> labels{
            Label(QStringLiteral("first"), QStringLiteral("框内"), {0.5, 0.5}),
            Label(QStringLiteral("second"), QStringLiteral("框外"), {0.7, 0.5}),
        };
        canvas.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        canvas.setVisibleGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        canvas.setImage(QStringLiteral("test.png"), QImage(200, 200, QImage::Format_ARGB32_Premultiplied), labels);

        const QPoint markerPosition = canvas.mapFromGlobal(canvas.globalPositionForLabel(0));
        QSignalSpy clickSpy(&canvas, &ImageCanvas::labelClicked);
        QSignalSpy moveSpy(&canvas, &ImageCanvas::labelMoveRequested);

        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, markerPosition);

        QCOMPARE(clickSpy.count(), 1);
        QCOMPARE(moveSpy.count(), 0);
        QCOMPARE(clickSpy.first().at(0).toInt(), 0);
        QVERIFY(qvariant_cast<Qt::KeyboardModifiers>(clickSpy.first().at(1)).testFlag(Qt::ControlModifier));
    }

    void imageCanvasCtrlDragMarkerMovesLabelWithoutStartingViewPan()
    {
        ImageCanvas canvas;
        canvas.resize(640, 480);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QVector<Label> labels{Label(QStringLiteral("first"), QStringLiteral("框内"), {0.5, 0.5})};
        canvas.setGroups({QStringLiteral("框内")});
        canvas.setVisibleGroups({QStringLiteral("框内")});
        canvas.setImage(QStringLiteral("test.png"), QImage(200, 200, QImage::Format_ARGB32_Premultiplied), labels);

        const QPoint markerPosition = canvas.mapFromGlobal(canvas.globalPositionForLabel(0));
        const QPoint dragPosition = markerPosition + QPoint(QApplication::startDragDistance() + 30, 0);
        QSignalSpy clickSpy(&canvas, &ImageCanvas::labelClicked);
        QSignalSpy moveSpy(&canvas, &ImageCanvas::labelMoveRequested);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, markerPosition);
        QTest::mouseMove(canvas.viewport(), dragPosition);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, dragPosition);

        QCOMPARE(clickSpy.count(), 0);
        QCOMPARE(moveSpy.count(), 1);
        QCOMPARE(moveSpy.first().at(0).toInt(), 0);
        const QPointF movedPosition = moveSpy.first().at(1).toPointF();
        QVERIFY(movedPosition.x() > 0.5);
    }

    void projectMergeUsesSingleInvolvedPageAutomatically()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_merge_single_test");
        QDir().mkpath(dirPath);

        labelqt::core::Project firstProject;
        firstProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        firstProject.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        firstProject.images().last().labels.append(Label(QStringLiteral("second"), QStringLiteral("框外"), {}));
        firstProject.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        firstProject.images().last().labels.append(Label(QStringLiteral("first"), QStringLiteral("框内"), {}));

        labelqt::core::Project secondProject;
        secondProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        secondProject.images().append(labelqt::core::ImageEntry{QStringLiteral("003.png"), {}, {}});
        secondProject.images().last().labels.append(Label(QStringLiteral("third"), QStringLiteral("框外"), {}));

        const QString firstPath = QDir(dirPath).filePath("first.txt");
        const QString secondPath = QDir(dirPath).filePath("second.txt");
        LabelPlusDocument::saveToFile(firstProject, firstPath);
        LabelPlusDocument::saveToFile(secondProject, secondPath);

        const auto plan = labelqt::services::ProjectMergeService::createPlan({firstPath, secondPath});

        QVERIFY(plan.conflicts.isEmpty());
        QCOMPARE(plan.mergedProject.images().size(), 3);
        QCOMPARE(plan.mergedProject.images().at(0).name, QStringLiteral("001.png"));
        QCOMPARE(plan.mergedProject.images().at(1).name, QStringLiteral("002.png"));
        QCOMPARE(plan.mergedProject.images().at(2).name, QStringLiteral("003.png"));
        QCOMPARE(plan.mergedProject.images().at(0).labels.first().text(), QStringLiteral("first"));
        QCOMPARE(plan.mergedProject.images().at(1).labels.first().text(), QStringLiteral("second"));
        QCOMPARE(plan.mergedProject.images().at(2).labels.first().text(), QStringLiteral("third"));

        const QString mergedPath = QDir(dirPath).filePath("merged.txt");
        const labelqt::core::Project merged =
            labelqt::services::ProjectMergeService::mergedProjectWithSelections(plan, {}, mergedPath);
        QCOMPARE(merged.commentLines().size(), 4);
        QCOMPARE(merged.commentLines().first(), QStringLiteral("# LabelQtMergeSources v2"));
        QCOMPARE(merged.commentLines().last(), QStringLiteral("# EndLabelQtMergeSources"));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"firstImage\":\"001.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"lastImage\":\"002.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"pageCount\":2")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"labelCount\":2")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"sourceIndex\":1")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"sourcePath\":\"first.txt\"")));
        QVERIFY(!merged.commentLines().at(1).contains(dirPath));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"firstImage\":\"003.png\"")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"lastImage\":\"003.png\"")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"pageCount\":1")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"sourceIndex\":2")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"sourcePath\":\"second.txt\"")));
        QVERIFY(!merged.commentLines().at(2).contains(dirPath));
    }

    void projectMergeCreatesConflictForMultipleInvolvedProjects()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_merge_conflict_test");
        QDir().mkpath(dirPath);

        labelqt::core::Project firstProject;
        firstProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        firstProject.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        firstProject.images().last().labels.append(Label(QStringLiteral("first"), QStringLiteral("框内"), {}));

        labelqt::core::Project secondProject;
        secondProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        secondProject.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        secondProject.images().last().labels.append(Label(QStringLiteral("second"), QStringLiteral("框外"), {}));

        const QString firstPath = QDir(dirPath).filePath("first.txt");
        const QString secondPath = QDir(dirPath).filePath("second.txt");
        LabelPlusDocument::saveToFile(firstProject, firstPath);
        LabelPlusDocument::saveToFile(secondProject, secondPath);

        const auto plan = labelqt::services::ProjectMergeService::createPlan({firstPath, secondPath});

        QCOMPARE(plan.conflicts.size(), 1);
        QCOMPARE(plan.conflicts.first().candidates.size(), 2);

        const QString mergedPath = QDir(dirPath).filePath("merged.txt");
        const labelqt::core::Project merged =
            labelqt::services::ProjectMergeService::mergedProjectWithSelections(plan, {1}, mergedPath);
        QCOMPARE(merged.images().size(), 1);
        QCOMPARE(merged.images().first().labels.first().text(), QStringLiteral("second"));
        QCOMPARE(merged.commentLines().size(), 3);
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"firstImage\":\"001.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"lastImage\":\"001.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"sourceIndex\":2")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"sourcePath\":\"second.txt\"")));
        QVERIFY(!merged.commentLines().at(1).contains(dirPath));
    }

    void projectMergeAppliesFinalPageOrderBeforeSourceComments()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_merge_page_order_test");
        QDir().mkpath(dirPath);

        labelqt::core::Project firstProject;
        firstProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        firstProject.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        firstProject.images().last().labels.append(Label(QStringLiteral("first"), QStringLiteral("框内"), {}));
        firstProject.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        firstProject.images().last().labels.append(Label(QStringLiteral("second"), QStringLiteral("框内"), {}));

        labelqt::core::Project secondProject;
        secondProject.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        secondProject.images().append(labelqt::core::ImageEntry{QStringLiteral("003.png"), {}, {}});
        secondProject.images().last().labels.append(Label(QStringLiteral("third"), QStringLiteral("框外"), {}));

        const QString firstPath = QDir(dirPath).filePath("first.txt");
        const QString secondPath = QDir(dirPath).filePath("second.txt");
        LabelPlusDocument::saveToFile(firstProject, firstPath);
        LabelPlusDocument::saveToFile(secondProject, secondPath);

        const auto plan = labelqt::services::ProjectMergeService::createPlan({firstPath, secondPath});
        const QString mergedPath = QDir(dirPath).filePath("merged.txt");
        const labelqt::core::Project merged =
            labelqt::services::ProjectMergeService::mergedProjectWithSelections(plan, {}, mergedPath, {2, 0, 1});

        QCOMPARE(merged.images().size(), 3);
        QCOMPARE(merged.images().at(0).name, QStringLiteral("003.png"));
        QCOMPARE(merged.images().at(1).name, QStringLiteral("001.png"));
        QCOMPARE(merged.images().at(2).name, QStringLiteral("002.png"));
        QCOMPARE(merged.commentLines().size(), 4);
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"firstImage\":\"003.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"lastImage\":\"003.png\"")));
        QVERIFY(merged.commentLines().at(1).contains(QStringLiteral("\"sourceIndex\":2")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"firstImage\":\"001.png\"")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"lastImage\":\"002.png\"")));
        QVERIFY(merged.commentLines().at(2).contains(QStringLiteral("\"sourceIndex\":1")));
    }

    void pageSourceInfoServiceExpandsMergeSourceRanges()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_page_source_info_test");
        QDir().mkpath(dirPath);

        labelqt::core::Project project;
        project.setFilePath(QDir(dirPath).filePath("merged.txt"));
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("003.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        project.setCommentLines({
            QStringLiteral("# LabelQtMergeSources v2"),
            QStringLiteral("# "
                           "{\"firstImage\":\"003.png\",\"lastImage\":\"003.png\",\"sourceIndex\":2,\"sourcePath\":"
                           "\"parts/b.txt\"}"),
            QStringLiteral(
                "# {\"firstImage\":\"001.png\",\"lastImage\":\"002.png\",\"sourceIndex\":1,\"sourcePath\":\"a.txt\"}"),
            QStringLiteral("# EndLabelQtMergeSources"),
        });

        const auto sources = labelqt::services::PageSourceInfoService::sourcesForProject(project);
        QCOMPARE(sources.size(), 3);
        QCOMPARE(sources.value(QStringLiteral("003.png")).sourceIndex, 2);
        QCOMPARE(sources.value(QStringLiteral("003.png")).sourcePath, QDir(dirPath).filePath("parts/b.txt"));
        QCOMPARE(sources.value(QStringLiteral("001.png")).sourceIndex, 1);
        QCOMPARE(sources.value(QStringLiteral("002.png")).sourceIndex, 1);
        QCOMPARE(sources.value(QStringLiteral("001.png")).sourcePath, QDir(dirPath).filePath("a.txt"));
    }

    void projectPageOrderServiceReordersImages()
    {
        labelqt::core::Project project;
        const QString dirPath = QDir::temp().filePath("labelqt_page_order_service_test");
        QDir().mkpath(dirPath);
        project.setFilePath(QDir(dirPath).filePath("merged.txt"));
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("003.png"), {}, {}});
        project.setCommentLines({
            QStringLiteral("# LabelQtMergeSources v2"),
            QStringLiteral(
                "# {\"firstImage\":\"001.png\",\"lastImage\":\"002.png\",\"sourceIndex\":1,\"sourcePath\":\"a.txt\"}"),
            QStringLiteral(
                "# {\"firstImage\":\"003.png\",\"lastImage\":\"003.png\",\"sourceIndex\":2,\"sourcePath\":\"b.txt\"}"),
            QStringLiteral("# EndLabelQtMergeSources"),
        });

        QVERIFY(labelqt::services::ProjectPageOrderService::isValidOrder({2, 0, 1}, 3));
        QVERIFY(labelqt::services::ProjectPageOrderService::isValidOrder({2, 0}, 3));
        QVERIFY(!labelqt::services::ProjectPageOrderService::isValidOrder({2, 2, 1}, 3));
        QVERIFY(labelqt::services::ProjectPageOrderService::isIdentityOrder({0, 1, 2}));

        labelqt::services::ProjectPageOrderService::reorderImages(project, {2, 0});
        QCOMPARE(project.images().size(), 2);
        QCOMPARE(project.images().at(0).name, QStringLiteral("003.png"));
        QCOMPARE(project.images().at(1).name, QStringLiteral("001.png"));

        const auto sources = labelqt::services::PageSourceInfoService::sourcesForProject(project);
        QCOMPARE(sources.value(QStringLiteral("003.png")).sourceIndex, 2);
        QCOMPARE(sources.value(QStringLiteral("001.png")).sourceIndex, 1);
        QVERIFY(project.commentLines().at(1).contains(QStringLiteral("\"firstImage\":\"003.png\"")));
        QVERIFY(project.commentLines().at(2).contains(QStringLiteral("\"firstImage\":\"001.png\"")));
        QVERIFY(project.commentLines().at(2).contains(QStringLiteral("\"lastImage\":\"001.png\"")));
    }

    void labelPlusDocumentPreservesCommentLines()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_comment_test");
        QDir().mkpath(dirPath);
        const QString filePath = QDir(dirPath).filePath("translation.txt");

        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.setSourceName(QStringLiteral("source.zip"));
        project.setCommentLines({QStringLiteral("# LabelQtMergeSources v1"),
                                 QStringLiteral("# {\"image\":\"001.png\",\"sourceIndex\":1}"),
                                 QStringLiteral("# EndLabelQtMergeSources")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("text"), QStringLiteral("框内"), {}));

        LabelPlusDocument::saveToFile(project, filePath);

        const auto reloaded = LabelPlusDocument::loadFromFile(filePath);
        QCOMPARE(reloaded.sourceName(), QStringLiteral("source.zip"));
        QCOMPARE(reloaded.commentLines(), project.commentLines());
        QCOMPARE(reloaded.images().size(), 1);
        QCOMPARE(reloaded.images().first().labels.first().text(), QStringLiteral("text"));
    }

    void preferencesReadMarkerFloatingPointSizes()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_preferences_test");
        QDir().mkpath(dirPath);
        const QString filePath = QDir(dirPath).filePath("preference.json");

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "{\n"
               << "  \"appearance\": {\n"
               << "    \"style\": \"Fusion\",\n"
               << "    \"theme\": \"breezeDark\"\n"
               << "  },\n"
               << "  \"labelMarker\": {\n"
               << "    \"diameter\": 4.5,\n"
               << "    \"fontPointSize\": 2.5\n"
               << "  },\n"
               << "  \"labelTable\": {\n"
               << "    \"fontFamily\": \"Noto Serif CJK SC\",\n"
               << "    \"fontPointSize\": 11.5,\n"
               << "    \"maxTextRows\": 4\n"
               << "  },\n"
               << "  \"labelTextEditor\": {\n"
               << "    \"fontFamily\": \"Noto Sans Mono\",\n"
               << "    \"fontPointSize\": 12.5\n"
               << "  },\n"
               << "  \"markerTextBubble\": {\n"
               << "    \"fontFamily\": \"Noto Sans CJK SC\",\n"
               << "    \"fontPointSize\": 9.5,\n"
               << "    \"opacity\": 0.75\n"
               << "  },\n"
               << "  \"canvasLabelTextEditor\": {\n"
               << "    \"opacity\": 0.6\n"
               << "  },\n"
               << "  \"input\": {\n"
               << "    \"moveLabelModifier\": \"ctrl+shift\",\n"
               << "    \"previousLabelModifier\": \"ctrl\",\n"
               << "    \"nextLabelShortcut\": \"Tab\",\n"
               << "    \"previousPageShortcut\": \"Alt+Left\",\n"
               << "    \"nextPageShortcut\": \"Alt+Right\",\n"
               << "    \"editLabelTextShortcut\": \"Return\",\n"
               << "    \"commitLabelTextShortcut\": \"Ctrl+Return\",\n"
               << "    \"undoShortcut\": \"Ctrl+Z\",\n"
               << "    \"redoShortcut\": \"Ctrl+Shift+Z\"\n"
               << "  },\n"
               << "  \"backupPath\": \"custom-bak\",\n"
               << "  \"backupIntervalSeconds\": 30,\n"
               << "  \"groupStyles\": [\n"
               << "    {\n"
               << "      \"groupColor\": \"#ff3835\",\n"
               << "      \"markerDiameter\": 4.5,\n"
               << "      \"fontPointSize\": 2.5,\n"
               << "      \"markerStyle\": \"circle\"\n"
               << "    },\n"
               << "    {\n"
               << "      \"groupColor\": \"#5ba8ec\",\n"
               << "      \"markerDiameter\": 5.5,\n"
               << "      \"fontPointSize\": 3.5,\n"
               << "      \"markerStyle\": \"square\"\n"
               << "    }\n"
               << "  ]\n"
               << "}\n";
        file.close();

        const auto result = labelqt::core::AppPreferences::loadFromFile(filePath);

        QVERIFY(result.warnings.isEmpty());
        QCOMPARE(result.preferences.labelMarkerDiameterPixels(), 4.5);
        QCOMPARE(result.preferences.labelMarkerFontPointSize(), 2.5);
        QCOMPARE(result.preferences.labelTableMaxTextRows(), 4);
        QCOMPARE(result.preferences.labelTableFontFamily(), QStringLiteral("Noto Serif CJK SC"));
        QCOMPARE(result.preferences.labelTableFontPointSize(), 11.5);
        QCOMPARE(result.preferences.labelTextEditorFontFamily(), QStringLiteral("Noto Sans Mono"));
        QCOMPARE(result.preferences.labelTextEditorFontPointSize(), 12.5);
        QCOMPARE(result.preferences.markerTextBubbleFontFamily(), QStringLiteral("Noto Sans CJK SC"));
        QCOMPARE(result.preferences.markerTextBubbleFontPointSize(), 9.5);
        QCOMPARE(result.preferences.markerTextBubbleOpacity(), 0.75);
        QCOMPARE(result.preferences.canvasLabelTextEditorOpacity(), 0.6);
        const labelqt::core::AppPreferencesLoadResult serializedResult =
            labelqt::core::AppPreferences::loadFromJson(result.preferences.toJsonDocument().toJson());
        QVERIFY(serializedResult.warnings.isEmpty());
        QCOMPARE(serializedResult.preferences.markerTextBubbleOpacity(), 0.75);
        QCOMPARE(serializedResult.preferences.canvasLabelTextEditorOpacity(), 0.6);
        QCOMPARE(serializedResult.preferences.alternateNextLabelShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Ctrl+Down"));
        QCOMPARE(result.preferences.applicationStyle(), QStringLiteral("Fusion"));
        QCOMPARE(result.preferences.applicationTheme(), QStringLiteral("breezeDark"));
        QCOMPARE(result.preferences.moveLabelModifiers(), Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(result.preferences.previousLabelModifiers(), Qt::ControlModifier);
        QCOMPARE(result.preferences.undoShortcut().toString(QKeySequence::PortableText), QStringLiteral("Ctrl+Z"));
        QCOMPARE(result.preferences.redoShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Ctrl+Shift+Z"));
        QCOMPARE(result.preferences.nextLabelShortcut().toString(QKeySequence::PortableText), QStringLiteral("Tab"));
        QCOMPARE(result.preferences.previousPageShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Alt+Left"));
        QCOMPARE(result.preferences.nextPageShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Alt+Right"));
        QCOMPARE(result.preferences.editLabelTextShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Return"));
        QCOMPARE(result.preferences.commitLabelTextShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Ctrl+Return"));
        QCOMPARE(result.preferences.backupPath(), QStringLiteral("custom-bak"));
        QCOMPARE(result.preferences.backupIntervalSeconds(), 30);
        QCOMPARE(result.preferences.groupStyles().size(), 2);
        QCOMPARE(result.preferences.groupStyles().at(0).groupColor, QColor(QStringLiteral("#ff3835")));
        QCOMPARE(result.preferences.groupStyles().at(1).markerDiameter, 5.5);
        QCOMPARE(result.preferences.groupStyles().at(1).fontPointSize, 3.5);
        QVERIFY(result.preferences.groupStyles().at(1).markerShape == labelqt::core::MarkerShape::Square);
    }

    void appPreferencesUsesDefaultGroupStyles()
    {
        const labelqt::core::AppPreferencesLoadResult result = labelqt::core::AppPreferences::loadFromJson("{}");

        QVERIFY(result.warnings.isEmpty());
        QCOMPARE(result.preferences.groupStyles().size(), 3);
        QCOMPARE(result.preferences.groupStyles().at(0).groupColor, QColor(QStringLiteral("#ef4444")));
        QCOMPARE(result.preferences.groupStyles().at(0).markerDiameter, 20.0);
        QCOMPARE(result.preferences.groupStyles().at(0).fontPointSize, 10.0);
        QVERIFY(result.preferences.groupStyles().at(0).markerShape == labelqt::core::MarkerShape::Circle);
        QCOMPARE(result.preferences.groupStyles().at(1).groupColor, QColor(QStringLiteral("#2563eb")));
        QVERIFY(result.preferences.groupStyles().at(1).markerShape == labelqt::core::MarkerShape::Square);
        QCOMPARE(result.preferences.groupStyles().at(2).groupColor, QColor(QStringLiteral("#10b981")));
        QVERIFY(result.preferences.groupStyles().at(2).markerShape == labelqt::core::MarkerShape::Circle);
    }

    void preferencesWarnOnInvalidJson()
    {
        const QString dirPath = QDir::temp().filePath("labelqt_preferences_test");
        QDir().mkpath(dirPath);
        const QString filePath = QDir(dirPath).filePath("broken-preference.json");

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("{");
        file.close();

        const auto result = labelqt::core::AppPreferences::loadFromFile(filePath);

        QVERIFY(!result.warnings.isEmpty());
        QCOMPARE(result.preferences.labelMarkerDiameterPixels(), 20.0);
        QCOMPARE(result.preferences.labelMarkerFontPointSize(), 10.0);
        QCOMPARE(result.preferences.labelTableMaxTextRows(), 3);
        QCOMPARE(result.preferences.labelTableFontFamily(), QString());
        QCOMPARE(result.preferences.labelTableFontPointSize(), 0.0);
        QCOMPARE(result.preferences.labelTextEditorFontFamily(), QString());
        QCOMPARE(result.preferences.labelTextEditorFontPointSize(), 0.0);
        QCOMPARE(result.preferences.markerTextBubbleFontFamily(), QString());
        QCOMPARE(result.preferences.markerTextBubbleFontPointSize(), 0.0);
        QCOMPARE(result.preferences.applicationStyle(), QString());
        QCOMPARE(result.preferences.applicationTheme(), QString());
        QCOMPARE(result.preferences.moveLabelModifiers(), Qt::ControlModifier);
        QCOMPARE(result.preferences.previousLabelModifiers(), Qt::ControlModifier);
        QCOMPARE(result.preferences.undoShortcut().toString(QKeySequence::PortableText), QStringLiteral("Ctrl+Z"));
        QCOMPARE(result.preferences.redoShortcut().toString(QKeySequence::PortableText), QStringLiteral("Ctrl+Y"));
        QCOMPARE(result.preferences.nextLabelShortcut().toString(QKeySequence::PortableText), QStringLiteral("Tab"));
        QCOMPARE(result.preferences.previousPageShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Alt+Left"));
        QCOMPARE(result.preferences.nextPageShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Alt+Right"));
        QCOMPARE(result.preferences.editLabelTextShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Return"));
        QCOMPARE(result.preferences.commitLabelTextShortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Ctrl+Return"));
        QCOMPARE(result.preferences.backupPath(), QStringLiteral("bak"));
        QCOMPARE(result.preferences.backupIntervalSeconds(), 60);
        QCOMPARE(result.preferences.groupStyles().size(), 3);
        QCOMPARE(result.preferences.groupStyles().at(0).groupColor, QColor(QStringLiteral("#ef4444")));
    }
};

QTEST_MAIN(LabelTests)

#include "LabelTests.moc"
