#include "core/AppPreferences.h"
#include "core/Label.h"
#include "core/LabelPlusDocument.h"
#include "core/Project.h"
#include "services/AutomationManifestParser.h"
#include "services/AutomationOperationApplier.h"
#include "services/AutomationService.h"
#include "services/LabelClipboardService.h"
#include "services/LabelEditController.h"
#include "services/LabelNavigator.h"
#include "services/LabelPastePlanner.h"
#include "services/PageSourceInfoService.h"
#include "services/ProjectController.h"
#include "services/ProjectImageValidator.h"
#include "services/ProjectMergeService.h"
#include "services/ProjectPageOrderService.h"
#include "services/SaveChangesDecision.h"
#include "services/SessionStateStore.h"
#include "ui/ImageCanvas.h"
#include "ui/LabelSelectionController.h"
#include "ui/LabelTableModel.h"
#include "ui/PageOrderListModel.h"

#include <QAbstractItemModelTester>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QMimeData>
#include <QSettings>
#include <QSet>
#include <QSignalSpy>
#include <QTableView>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest/QtTest>

#include <expected>
#include <memory>
#include <optional>

using labelqt::core::Label;
using labelqt::core::LabelPlusDocument;

namespace {

labelqt::services::LabelEditCommandTexts testCommandTexts()
{
    return {
        QStringLiteral("Add"),      QStringLiteral("Edit"),       QStringLiteral("Group"),
        QStringLiteral("Move"),     QStringLiteral("Delete"),     QStringLiteral("Reorder"),
        QStringLiteral("Paste"),    QStringLiteral("Add group"),  QStringLiteral("Remove group"),
        QStringLiteral("Add %1 %2 label %3"),
        QStringLiteral("Delete %1 %2 label %3"),
        QStringLiteral("Edit %1 %2 label %3"),
        QStringLiteral("Change %1 %2 label %3"),
        QStringLiteral("Move %1 %2 label %3"),
        QStringLiteral("Reorder labels on %1"),
        QStringLiteral("Paste %2 label(s) on %1"),
        QStringLiteral("Add group %1"),
        QStringLiteral("Remove group %1"),
    };
}

} // namespace

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

    void saveChangesDecisionMapsPromptChoices()
    {
        using labelqt::services::SaveChangesAction;
        using labelqt::services::SaveChangesChoice;
        using labelqt::services::SaveChangesDecision;

        QCOMPARE(SaveChangesDecision::actionForChoice(SaveChangesChoice::Save),
                 SaveChangesAction::SaveThenContinue);
        QCOMPARE(SaveChangesDecision::actionForChoice(SaveChangesChoice::Discard),
                 SaveChangesAction::ContinueWithoutSaving);
        QCOMPARE(SaveChangesDecision::actionForChoice(SaveChangesChoice::Cancel), SaveChangesAction::Cancel);
    }

    void labelClipboardRoundTripsLabels()
    {
        labelqt::services::ClipboardLabels labels;
        labels.sourceImageName = QStringLiteral("001.png");
        labels.pasteBehavior = labelqt::services::ClipboardLabels::PasteBehavior::PreservePositionOnPaste;
        labels.labels.append(Label(QStringLiteral("hello"), QStringLiteral("框内"), QPointF(0.2, 0.3)));
        labels.labels.append(Label(QStringLiteral("world"), QStringLiteral("框外"), QPointF(0.7, 0.8)));

        std::unique_ptr<QMimeData> mimeData(labelqt::services::LabelClipboardService::createMimeData(labels));
        QVERIFY(mimeData->hasFormat(QString::fromLatin1(labelqt::services::LabelClipboardService::mimeType)));
        QCOMPARE(mimeData->text(), QStringLiteral("hello\n\nworld"));

        const labelqt::services::ClipboardLabels restored =
            labelqt::services::LabelClipboardService::readMimeData(mimeData.get());
        const std::expected<labelqt::services::ClipboardLabels, QString> expectedRestored =
            labelqt::services::LabelClipboardService::tryReadMimeData(mimeData.get());
        QVERIFY(expectedRestored.has_value());
        QCOMPARE(restored.sourceImageName, QStringLiteral("001.png"));
        QCOMPARE(restored.pasteBehavior,
                 labelqt::services::ClipboardLabels::PasteBehavior::PreservePositionOnPaste);
        QCOMPARE(restored.labels.size(), 2);
        QCOMPARE(restored.labels.at(0).text(), QStringLiteral("hello"));
        QCOMPARE(restored.labels.at(0).group(), QStringLiteral("框内"));
        QCOMPARE(restored.labels.at(0).position(), QPointF(0.2, 0.3));
        QCOMPARE(restored.labels.at(1).text(), QStringLiteral("world"));
    }

    void labelClipboardReportsInvalidData()
    {
        QMimeData mimeData;
        mimeData.setData(QString::fromLatin1(labelqt::services::LabelClipboardService::mimeType),
                         QByteArrayLiteral("{not-json"));

        const std::expected<labelqt::services::ClipboardLabels, QString> restored =
            labelqt::services::LabelClipboardService::tryReadMimeData(&mimeData);
        QVERIFY(!restored.has_value());
        QVERIFY(restored.error().contains(QStringLiteral("not valid JSON")));

        const labelqt::services::ClipboardLabels fallback =
            labelqt::services::LabelClipboardService::readMimeData(&mimeData);
        QVERIFY(fallback.labels.isEmpty());
    }

    void pastedLabelsAreInsertedAfterSelectedLabelAndUndoable()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("a"), QStringLiteral("框内"), QPointF(0.1, 0.1)));
        project.images().last().labels.append(Label(QStringLiteral("b"), QStringLiteral("框外"), QPointF(0.2, 0.2)));

        labelqt::core::UndoStack undoStack;
        QVector<int> selectedIndexes;
        labelqt::services::LabelEditController controller(project, undoStack, testCommandTexts());
        controller.setCallbacks({}, [&selectedIndexes](int, QVector<int> indexes) { selectedIndexes = indexes; }, {},
                                {}, {});

        QVector<Label> pastedLabels{
            Label(QStringLiteral("p1"), QStringLiteral("框内"), QPointF(0.3, 0.3)),
            Label(QStringLiteral("p2"), QStringLiteral("框外"), QPointF(0.4, 0.4)),
        };
        const labelqt::services::LabelEditResult result = controller.pasteLabels(0, pastedLabels, 0);

        QVERIFY(result.changed);
        QCOMPARE(result.selectedLabelIndexes, QVector<int>({1, 2}));
        QCOMPARE(project.images().first().labels.size(), 4);
        QCOMPARE(project.images().first().labels.at(0).text(), QStringLiteral("a"));
        QCOMPARE(project.images().first().labels.at(1).text(), QStringLiteral("p1"));
        QCOMPARE(project.images().first().labels.at(2).text(), QStringLiteral("p2"));
        QCOMPARE(project.images().first().labels.at(3).text(), QStringLiteral("b"));

        undoStack.undo();
        QCOMPARE(project.images().first().labels.size(), 2);
        QVERIFY(selectedIndexes.isEmpty());

        undoStack.redo();
        QCOMPARE(project.images().first().labels.size(), 4);
        QCOMPARE(selectedIndexes, QVector<int>({1, 2}));
    }

    void cutPasteUndoRedoRestoresSelectionState()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("a"), QStringLiteral("框内"), QPointF(0.1, 0.1)));
        project.images().last().labels.append(Label(QStringLiteral("b"), QStringLiteral("框外"), QPointF(0.2, 0.2)));
        project.images().last().labels.append(Label(QStringLiteral("c"), QStringLiteral("框内"), QPointF(0.3, 0.3)));

        labelqt::core::UndoStack undoStack;
        QVector<int> selectedIndexes;
        int clearedImageIndex = -1;
        labelqt::services::LabelEditController controller(project, undoStack, testCommandTexts());
        controller.setCallbacks({}, [&selectedIndexes](int, QVector<int> indexes) { selectedIndexes = indexes; },
                                [&clearedImageIndex, &selectedIndexes](int imageIndex) {
                                    clearedImageIndex = imageIndex;
                                    selectedIndexes.clear();
                                },
                                {}, {});

        QVector<Label> cutLabels{
            project.images().first().labels.at(0),
            project.images().first().labels.at(1),
        };
        labelqt::services::LabelEditResult deleteResult = controller.deleteLabels(0, {0, 1});
        QVERIFY(deleteResult.changed);
        QVERIFY(project.images().first().labels.at(0).isDeleted());
        QVERIFY(project.images().first().labels.at(1).isDeleted());

        undoStack.undo();
        QCOMPARE(selectedIndexes, QVector<int>({0, 1}));
        QVERIFY(!project.images().first().labels.at(0).isDeleted());
        QVERIFY(!project.images().first().labels.at(1).isDeleted());

        undoStack.redo();
        QCOMPARE(clearedImageIndex, 0);
        QVERIFY(project.images().first().labels.at(0).isDeleted());
        QVERIFY(project.images().first().labels.at(1).isDeleted());

        labelqt::services::LabelEditResult pasteResult = controller.pasteLabels(0, cutLabels, 2);
        QVERIFY(pasteResult.changed);
        QCOMPARE(pasteResult.selectedLabelIndexes, QVector<int>({3, 4}));
        QCOMPARE(project.images().first().labels.at(3).text(), QStringLiteral("a"));
        QCOMPARE(project.images().first().labels.at(4).text(), QStringLiteral("b"));

        undoStack.undo();
        QVERIFY(selectedIndexes.isEmpty());
        QCOMPARE(project.images().first().labels.size(), 3);

        undoStack.redo();
        QCOMPARE(project.images().first().labels.size(), 5);
        QCOMPARE(selectedIndexes, QVector<int>({3, 4}));
    }

    void batchMoveLabelsIsUndoableAndRestoresSelection()
    {
        labelqt::core::Project project;
        project.setGroups({QStringLiteral("框内")});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().last().labels.append(Label(QStringLiteral("a"), QStringLiteral("框内"), QPointF(0.2, 0.2)));
        project.images().last().labels.append(Label(QStringLiteral("b"), QStringLiteral("框内"), QPointF(0.4, 0.4)));
        project.images().last().labels.append(Label(QStringLiteral("c"), QStringLiteral("框内"), QPointF(0.8, 0.8)));

        labelqt::core::UndoStack undoStack;
        QVector<int> selectedIndexes;
        labelqt::services::LabelEditController controller(project, undoStack, testCommandTexts());
        controller.setCallbacks({}, [&selectedIndexes](int, QVector<int> indexes) { selectedIndexes = indexes; }, {},
                                {}, {});

        const labelqt::services::LabelEditResult result =
            controller.setLabelPositions(0, {0, 1}, {QPointF(0.3, 0.3), QPointF(0.5, 0.5)});

        QVERIFY(result.changed);
        QCOMPARE(result.selectedLabelIndexes, QVector<int>({0, 1}));
        QCOMPARE(project.images().first().labels.at(0).position(), QPointF(0.3, 0.3));
        QCOMPARE(project.images().first().labels.at(1).position(), QPointF(0.5, 0.5));

        undoStack.undo();
        QCOMPARE(project.images().first().labels.at(0).position(), QPointF(0.2, 0.2));
        QCOMPARE(project.images().first().labels.at(1).position(), QPointF(0.4, 0.4));
        QCOMPARE(selectedIndexes, QVector<int>({0, 1}));

        undoStack.redo();
        QCOMPARE(project.images().first().labels.at(0).position(), QPointF(0.3, 0.3));
        QCOMPARE(project.images().first().labels.at(1).position(), QPointF(0.5, 0.5));
        QCOMPARE(selectedIndexes, QVector<int>({0, 1}));
    }

    void pastedLabelPositionsStayInsideImageBounds()
    {
        labelqt::core::ImageEntry image{QStringLiteral("001.png"), {}, {}};
        image.labels.append(Label(QStringLiteral("existing"), QStringLiteral("框内"), QPointF(0.98, 0.98)));
        const QVector<Label> labels{
            Label(QStringLiteral("pasted"), QStringLiteral("框内"), QPointF(0.99, 0.99)),
        };

        const QVector<Label> adjusted = labelqt::services::LabelPastePlanner::labelsAdjustedForPaste(
            labels, image, QSet<QString>{QStringLiteral("框内")},
            {labelqt::services::LabelPastePlanner::PasteBehavior::OffsetOnPaste, std::nullopt});

        QCOMPARE(adjusted.size(), 1);
        QVERIFY(adjusted.first().position().x() >= 0.0);
        QVERIFY(adjusted.first().position().x() <= 1.0);
        QVERIFY(adjusted.first().position().y() >= 0.0);
        QVERIFY(adjusted.first().position().y() <= 1.0);
    }

    void cutPastePreservesOriginalPosition()
    {
        labelqt::core::ImageEntry image{QStringLiteral("001.png"), {}, {}};
        image.labels.append(Label(QStringLiteral("existing"), QStringLiteral("框内"), QPointF(0.2, 0.2)));
        const QVector<Label> labels{
            Label(QStringLiteral("cut"), QStringLiteral("框内"), QPointF(0.2, 0.2)),
        };

        const QVector<Label> adjusted = labelqt::services::LabelPastePlanner::labelsAdjustedForPaste(
            labels, image, QSet<QString>{QStringLiteral("框内")},
            {labelqt::services::LabelPastePlanner::PasteBehavior::PreservePositionOnPaste, std::nullopt});

        QCOMPARE(adjusted.size(), 1);
        QCOMPARE(adjusted.first().position(), QPointF(0.2, 0.2));
    }

    void anchoredPasteMovesLabelsToAnchor()
    {
        labelqt::core::ImageEntry image{QStringLiteral("001.png"), {}, {}};
        const QVector<Label> labels{
            Label(QStringLiteral("a"), QStringLiteral("框内"), QPointF(0.2, 0.2)),
            Label(QStringLiteral("b"), QStringLiteral("框内"), QPointF(0.4, 0.4)),
        };

        const QVector<Label> adjusted = labelqt::services::LabelPastePlanner::labelsAdjustedForPaste(
            labels, image, QSet<QString>{QStringLiteral("框内")},
            {labelqt::services::LabelPastePlanner::PasteBehavior::OffsetOnPaste, QPointF(0.6, 0.6)});

        QCOMPARE(adjusted.size(), 2);
        QCOMPARE(adjusted.at(0).position(), QPointF(0.5, 0.5));
        QCOMPARE(adjusted.at(1).position(), QPointF(0.7, 0.7));
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

    void automationManifestRejectsInvalidOperationsOutput()
    {
        QJsonObject output;
        output.insert(QStringLiteral("operations"), QStringLiteral("not-an-array"));

        const std::expected<labelqt::services::AutomationRunResult, QString> result =
            labelqt::services::AutomationManifestParser::tryResultFromOutput(output);

        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains(QStringLiteral("operations")));
    }

    void automationManifestParsesQuietResultAndOperations()
    {
        QJsonObject operation;
        operation.insert(QStringLiteral("type"), QStringLiteral("setLabelText"));
        operation.insert(QStringLiteral("page"), QStringLiteral("001.png"));
        operation.insert(QStringLiteral("labelIndex"), 2);
        operation.insert(QStringLiteral("text"), QStringLiteral("translated"));

        QJsonObject resultObject;
        resultObject.insert(QStringLiteral("title"), QStringLiteral("OCR result"));
        resultObject.insert(QStringLiteral("text"), QStringLiteral("recognized text"));

        QJsonObject output;
        output.insert(QStringLiteral("summary"), QStringLiteral("done"));
        output.insert(QStringLiteral("quiet"), true);
        output.insert(QStringLiteral("result"), resultObject);
        output.insert(QStringLiteral("operations"), QJsonArray{operation});

        const std::expected<labelqt::services::AutomationRunResult, QString> parsed =
            labelqt::services::AutomationManifestParser::tryResultFromOutput(output);
        QVERIFY(parsed.has_value());
        QVERIFY(parsed->quiet);
        QCOMPARE(parsed->summary, QStringLiteral("done"));
        QCOMPARE(parsed->resultTitle, QStringLiteral("OCR result"));
        QCOMPARE(parsed->resultText, QStringLiteral("recognized text"));
        QCOMPARE(parsed->operations.size(), 1);
        QCOMPARE(parsed->operations.first().type, QStringLiteral("setLabelText"));
        QCOMPARE(parsed->operations.first().page, QStringLiteral("001.png"));
        QCOMPARE(parsed->operations.first().labelIndex, 2);
        QCOMPARE(parsed->operations.first().text, QStringLiteral("translated"));
    }

    void automationRunnerReportsMissingPythonAsFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString scriptPath = directory.filePath(QStringLiteral("script.py"));
        QFile scriptFile(scriptPath);
        QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));
        scriptFile.write("print('should not run')\n");
        scriptFile.close();

        labelqt::services::AutomationScript script;
        script.id = QStringLiteral("test:missing-python");
        script.name = QStringLiteral("Missing Python");
        script.directoryPath = directory.path();
        script.entryPath = scriptPath;

        labelqt::core::Project project;
        labelqt::services::AutomationPythonSettings pythonSettings;
        pythonSettings.command = directory.filePath(QStringLiteral("definitely_missing_python"));

        labelqt::services::AutomationRunner runner;
        bool finished = false;
        labelqt::services::AutomationRunResult result;
        connect(&runner, &labelqt::services::AutomationRunner::finished, this,
                [&finished, &result](const labelqt::services::AutomationRunResult& runResult) {
                    result = runResult;
                    finished = true;
                });

        runner.start(script, project, -1, {}, {}, {}, {}, pythonSettings);
        if (!finished) {
            QEventLoop loop;
            connect(&runner, &labelqt::services::AutomationRunner::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        }

        QVERIFY(finished);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.error.contains(QStringLiteral("Python")) || result.error.contains(QStringLiteral("Failed")));
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

    void groupFilterSelectionDropsHiddenLabels()
    {
        labelqt::core::ImageEntry image{QStringLiteral("001.png"), {}, {}};
        image.labels.append(Label(QStringLiteral("inside"), QStringLiteral("框内"), {0.2, 0.3}));
        image.labels.append(Label(QStringLiteral("outside"), QStringLiteral("框外"), {0.4, 0.5}));
        image.labels.append(Label(QStringLiteral("inside 2"), QStringLiteral("框内"), {0.6, 0.7}));

        LabelTableModel model;
        model.setGroups({QStringLiteral("框内"), QStringLiteral("框外")}, {});
        model.setLabels(&image.labels);
        model.setGroupFilter({QStringLiteral("框内"), QStringLiteral("框外")});

        QTableView tableView;
        tableView.setModel(&model);
        ImageCanvas canvas;
        canvas.setGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        canvas.setVisibleGroups({QStringLiteral("框内"), QStringLiteral("框外")});
        canvas.setImage(QStringLiteral("test.png"), QImage(200, 200, QImage::Format_ARGB32_Premultiplied),
                        image.labels);

        LabelSelectionController controller;
        QVector<int> detailsUpdates;
        controller.setWidgets(&model, &tableView, &canvas);
        controller.setCallbacks(
            [&image, &detailsUpdates](int sourceIndex) {
                const bool valid = sourceIndex >= 0 && sourceIndex < image.labels.size();
                if (valid) {
                    detailsUpdates.append(sourceIndex);
                }
                return valid;
            },
            []() {}, {}, {});

        QVERIFY(controller.selectIndexes(image, {0, 1}, 1));
        QCOMPARE(controller.selectedLabelIndexes(), QVector<int>({0, 1}));

        const QVector<int> previousSelectedIndexes = controller.selectedLabelIndexes();
        model.setGroupFilter({QStringLiteral("框内")});
        canvas.setVisibleGroups({QStringLiteral("框内")});
        QVERIFY(controller.selectIndexes(image, previousSelectedIndexes, 1));
        QCOMPARE(controller.selectedLabelIndexes(), QVector<int>({0}));
        QCOMPARE(detailsUpdates.last(), 0);
    }

    void labelTableModelEditRequestsDoNotMutateLabelsDirectly()
    {
        QVector<Label> labels{
            Label(QStringLiteral("inside"), QStringLiteral("框内"), {0.2, 0.3}),
            Label(QStringLiteral("outside"), QStringLiteral("框外"), {0.4, 0.5}),
        };

        LabelTableModel model;
        model.setGroups({QStringLiteral("框内"), QStringLiteral("框外")}, {});
        model.setLabels(&labels);
        model.setGroupFilter({QStringLiteral("框内"), QStringLiteral("框外")});

        QSignalSpy editSpy(&model, &LabelTableModel::labelEditRequested);
        QVERIFY(model.setData(model.index(0, LabelTableModel::TextColumn), QStringLiteral("changed"), Qt::EditRole));
        QCOMPARE(editSpy.count(), 1);
        QCOMPARE(editSpy.first().at(0).toInt(), 0);
        QCOMPARE(editSpy.first().at(1).toInt(), static_cast<int>(LabelTableModel::TextColumn));
        QCOMPARE(editSpy.first().at(2).toString(), QStringLiteral("changed"));
        QCOMPARE(labels.first().text(), QStringLiteral("inside"));

        QVERIFY(!model.setData(model.index(0, LabelTableModel::GroupColumn), QStringLiteral("不存在"), Qt::EditRole));
        QCOMPARE(editSpy.count(), 1);
    }

    void labelTableModelDropRequestsSourceIndexesInVisibleOrder()
    {
        QVector<Label> labels{
            Label(QStringLiteral("a"), QStringLiteral("框内"), {0.1, 0.1}),
            Label(QStringLiteral("b"), QStringLiteral("框外"), {0.2, 0.2}),
            Label(QStringLiteral("c"), QStringLiteral("框内"), {0.3, 0.3}),
        };

        LabelTableModel model;
        model.setGroups({QStringLiteral("框内"), QStringLiteral("框外")}, {});
        model.setLabels(&labels);
        model.setGroupFilter({QStringLiteral("框内"), QStringLiteral("框外")});

        const QModelIndexList draggedIndexes{
            model.index(0, LabelTableModel::NumberColumn),
            model.index(0, LabelTableModel::TextColumn),
            model.index(2, LabelTableModel::NumberColumn),
        };
        std::unique_ptr<QMimeData> mimeData(model.mimeData(draggedIndexes));
        QSignalSpy reorderSpy(&model, &LabelTableModel::labelsReorderRequested);

        QVERIFY(model.dropMimeData(mimeData.get(), Qt::MoveAction, 1, 0, {}));
        QCOMPARE(reorderSpy.count(), 1);
        QCOMPARE(qvariant_cast<QVector<int>>(reorderSpy.first().at(0)), QVector<int>({0, 2}));
        QCOMPARE(reorderSpy.first().at(1).toInt(), 1);
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

    void pageOrderListModelRemoveKeepsReasonableSelection()
    {
        labelqt::core::Project project;
        for (int i = 0; i < 4; ++i) {
            project.images().append(
                labelqt::core::ImageEntry{QStringLiteral("%1.png").arg(i, 3, 10, QLatin1Char('0')), {}, {}});
        }

        PageOrderListModel model(project);
        model.removeSourceIndexes({1, 2});

        QCOMPARE(model.pageOrder(), QVector<int>({0, 3}));
        QCOMPARE(model.lastMovedSourceIndexes(), QVector<int>({3}));
        QCOMPARE(model.rowForSourceIndex(3), 1);
        QCOMPARE(project.images().size(), 4);
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

    void projectControllerExpectedInterfacesReportSkippedAndNoImages()
    {
        const QString dirPath = QDir::temp().filePath(QStringLiteral("labelqt_project_controller_expected_test"));
        QDir directory(dirPath);
        if (directory.exists()) {
            directory.removeRecursively();
        }
        QVERIFY(QDir().mkpath(dirPath));

        labelqt::services::ProjectController controller;
        const std::expected<QString, labelqt::services::NewProjectError> newProject =
            controller.tryCreateProjectFromImageDirectory(dirPath, QStringLiteral("translation"),
                                                          {QStringLiteral("框内"), QStringLiteral("框外")}, false);
        QVERIFY(!newProject.has_value());
        QCOMPARE(newProject.error().code, labelqt::services::NewProjectError::Code::NoImages);

        const std::expected<std::optional<QString>, QString> backup =
            controller.tryPerformAutoBackup(labelqt::core::AppPreferences{});
        QVERIFY(backup.has_value());
        QVERIFY(!backup->has_value());
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

    void imageCanvasCtrlDragSelectedMarkersMovesSelectionTogether()
    {
        ImageCanvas canvas;
        canvas.resize(640, 480);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QVector<Label> labels{
            Label(QStringLiteral("first"), QStringLiteral("框内"), {0.4, 0.5}),
            Label(QStringLiteral("second"), QStringLiteral("框内"), {0.6, 0.5}),
        };
        canvas.setGroups({QStringLiteral("框内")});
        canvas.setVisibleGroups({QStringLiteral("框内")});
        canvas.setImage(QStringLiteral("test.png"), QImage(200, 200, QImage::Format_ARGB32_Premultiplied), labels);
        canvas.setSelectedLabels({0, 1});

        const QPoint markerPosition = canvas.mapFromGlobal(canvas.globalPositionForLabel(0));
        const QPoint dragPosition = markerPosition + QPoint(QApplication::startDragDistance() + 30, 0);
        QSignalSpy singleMoveSpy(&canvas, &ImageCanvas::labelMoveRequested);
        QSignalSpy batchMoveSpy(&canvas, &ImageCanvas::labelsMoveRequested);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, markerPosition);
        QTest::mouseMove(canvas.viewport(), dragPosition);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, dragPosition);

        QCOMPARE(singleMoveSpy.count(), 0);
        QCOMPARE(batchMoveSpy.count(), 1);
        const QVector<int> movedIndexes = qvariant_cast<QVector<int>>(batchMoveSpy.first().at(0));
        const QVector<QPointF> movedPositions = qvariant_cast<QVector<QPointF>>(batchMoveSpy.first().at(1));
        QCOMPARE(movedIndexes, QVector<int>({0, 1}));
        QCOMPARE(movedPositions.size(), 2);
        QVERIFY(movedPositions.at(0).x() > 0.4);
        QVERIFY(movedPositions.at(1).x() > 0.6);
    }

    void imageCanvasCtrlDragUnselectedMarkerMovesOnlyThatMarker()
    {
        ImageCanvas canvas;
        canvas.resize(640, 480);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QVector<Label> labels{
            Label(QStringLiteral("first"), QStringLiteral("框内"), {0.4, 0.5}),
            Label(QStringLiteral("second"), QStringLiteral("框内"), {0.6, 0.5}),
        };
        canvas.setGroups({QStringLiteral("框内")});
        canvas.setVisibleGroups({QStringLiteral("框内")});
        canvas.setImage(QStringLiteral("test.png"), QImage(200, 200, QImage::Format_ARGB32_Premultiplied), labels);
        canvas.setSelectedLabels({0});

        connect(&canvas, &ImageCanvas::labelSelected, &canvas, [&canvas](int index) { canvas.setSelectedLabel(index); });
        QSignalSpy selectionSpy(&canvas, &ImageCanvas::labelSelected);
        QSignalSpy singleMoveSpy(&canvas, &ImageCanvas::labelMoveRequested);
        QSignalSpy batchMoveSpy(&canvas, &ImageCanvas::labelsMoveRequested);

        const QPoint markerPosition = canvas.mapFromGlobal(canvas.globalPositionForLabel(1));
        const QPoint dragPosition = markerPosition + QPoint(QApplication::startDragDistance() + 30, 0);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, markerPosition);
        QTest::mouseMove(canvas.viewport(), dragPosition);
        QCOMPARE(selectionSpy.count(), 1);
        QCOMPARE(selectionSpy.first().at(0).toInt(), 1);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::ControlModifier, dragPosition);

        QCOMPARE(batchMoveSpy.count(), 0);
        QCOMPARE(singleMoveSpy.count(), 1);
        QCOMPARE(singleMoveSpy.first().at(0).toInt(), 1);
        const QPointF movedPosition = singleMoveSpy.first().at(1).toPointF();
        QVERIFY(movedPosition.x() > 0.6);
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

    void projectPageOrderServiceKeepsSourcesAttachedToImageNames()
    {
        labelqt::core::Project project;
        const QString dirPath = QDir::temp().filePath("labelqt_page_order_sources_test");
        QDir().mkpath(dirPath);
        project.setFilePath(QDir(dirPath).filePath("merged.txt"));
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("001.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("002.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("003.png"), {}, {}});
        project.images().append(labelqt::core::ImageEntry{QStringLiteral("004.png"), {}, {}});
        project.setCommentLines({
            QStringLiteral("# ordinary comment"),
            QStringLiteral("# LabelQtMergeSources v2"),
            QStringLiteral(
                "# {\"firstImage\":\"001.png\",\"lastImage\":\"002.png\",\"sourceIndex\":1,\"sourcePath\":\"a.txt\"}"),
            QStringLiteral(
                "# {\"firstImage\":\"003.png\",\"lastImage\":\"004.png\",\"sourceIndex\":2,\"sourcePath\":\"b.txt\"}"),
            QStringLiteral("# EndLabelQtMergeSources"),
        });

        labelqt::services::ProjectPageOrderService::reorderImages(project, {2, 0, 3, 1});

        QCOMPARE(project.images().at(0).name, QStringLiteral("003.png"));
        QCOMPARE(project.images().at(1).name, QStringLiteral("001.png"));
        QCOMPARE(project.images().at(2).name, QStringLiteral("004.png"));
        QCOMPARE(project.images().at(3).name, QStringLiteral("002.png"));

        const auto sources = labelqt::services::PageSourceInfoService::sourcesForProject(project);
        QCOMPARE(sources.value(QStringLiteral("001.png")).sourceIndex, 1);
        QCOMPARE(sources.value(QStringLiteral("002.png")).sourceIndex, 1);
        QCOMPARE(sources.value(QStringLiteral("003.png")).sourceIndex, 2);
        QCOMPARE(sources.value(QStringLiteral("004.png")).sourceIndex, 2);
        QCOMPARE(sources.value(QStringLiteral("001.png")).sourcePath, QDir(dirPath).filePath(QStringLiteral("a.txt")));
        QCOMPARE(sources.value(QStringLiteral("003.png")).sourcePath, QDir(dirPath).filePath(QStringLiteral("b.txt")));
        QCOMPARE(project.commentLines().first(), QStringLiteral("# ordinary comment"));
        QStringList sourceLines;
        for (const QString& line : project.commentLines()) {
            if (line.contains(QStringLiteral("\"firstImage\""))) {
                sourceLines.append(line);
            }
        }
        QCOMPARE(sourceLines.size(), 4);
        QVERIFY(sourceLines.at(0).contains(QStringLiteral("\"firstImage\":\"003.png\"")));
        QVERIFY(sourceLines.at(1).contains(QStringLiteral("\"firstImage\":\"001.png\"")));
        QVERIFY(sourceLines.at(2).contains(QStringLiteral("\"firstImage\":\"004.png\"")));
        QVERIFY(sourceLines.at(3).contains(QStringLiteral("\"firstImage\":\"002.png\"")));
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
