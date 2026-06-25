#include "ui/MainWindow.h"

#include "services/AutomationOperationApplier.h"
#include "services/LabelNavigator.h"
#include "services/ProjectImageValidator.h"
#include "services/SessionStateStore.h"
#include "ui/AutomationController.h"
#include "ui/AutomationShortcutController.h"
#include "ui/CanvasLabelTextEditController.h"
#include "ui/EditorStateController.h"
#include "ui/ImagePageViewController.h"
#include "ui/LabelEditDelegates.h"
#include "ui/LabelSelectionController.h"
#include "ui/MainWindowShortcutController.h"
#include "ui/PageOrderDialog.h"
#include "ui/PageSelectorComboBox.h"
#include "ui/PreferenceDialog.h"
#include "ui/ProjectMergeDialog.h"
#include "ui/ProjectViewController.h"
#include "ui/ThemeManager.h"
#include "ui/ViewportFittedTableColumns.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QStringView>
#include <QStyle>
#include <QStyleFactory>
#include <QTableView>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
constexpr int defaultLabelNumberColumnWidth = 72;
constexpr int defaultLabelTextColumnWidth = 560;
constexpr int defaultLabelGroupColumnWidth = 120;
constexpr int minimumLabelNumberColumnWidth = 40;
constexpr int minimumLabelTextColumnWidth = 120;
constexpr int minimumLabelGroupColumnWidth = 60;

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_labelModel(new LabelTableModel(this)), m_labelTextDelegate(new LabelTextDelegate(this)),
      m_labelGroupDelegate(new LabelGroupDelegate(this))
{
    const labelqt::core::AppPreferencesLoadResult preferences = labelqt::core::AppPreferences::loadWithDiagnostics();
    m_preferences = preferences.preferences;
    m_preferenceWarnings = preferences.warnings;
    qApp->installEventFilter(this);
    m_shortcutController = new MainWindowShortcutController(this, this);
    m_shortcutController->setPreferences(m_preferences);
    m_shortcutController->setCallbacks({
        [this]() { selectAdjacentVisibleLabelFromShortcut(true); },
        [this]() { selectAdjacentVisibleLabelFromShortcut(false); },
        [this]() {
            commitActiveTextInput();
            selectPreviousPage();
        },
        [this]() {
            commitActiveTextInput();
            selectNextPage();
        },
        [this]() { editCurrentLabelText(); },
    });
    m_automationController = new AutomationController(this, this);
    m_automationController->setPreferences(m_preferences);
    m_automationController->setCallbacks({
        [this]() { return project().isEmpty(); },
        [this]() { commitActiveTextInput(); },
        [this]() -> const labelqt::core::Project& { return project(); },
        [this]() { return project().groups(); },
        [this]() { return m_preferences.groupStyles(); },
        [this]() { return m_currentImageIndex; },
        [this]() {
            labelqt::services::AutomationSelection selection;
            if (m_canvas != nullptr && m_canvas->hasSelection()) {
                selection.hasSelection = true;
                selection.normalizedRect = m_canvas->normalizedSelectionRect();
            }
            return selection;
        },
        [this]() {
            labelqt::services::AutomationContext context;
            context.currentImageIndex = m_currentImageIndex;
            context.selectedLabelIndexes = selectedLabelIndexes();
            return context;
        },
    });
    m_automationShortcutController = new AutomationShortcutController(this, this);
    m_automationShortcutController->setPreferences(m_preferences);
    connect(m_automationController, &AutomationController::scriptsChanged, m_automationShortcutController,
            &AutomationShortcutController::setScripts);
    connect(m_automationController, &AutomationController::discoveryWarningsFound, this,
            &MainWindow::showAutomationDiscoveryWarnings);
    connect(m_automationController, &AutomationController::runningChanged, this, &MainWindow::setAutomationRunning);
    connect(m_automationController, &AutomationController::operationsReady, this,
            &MainWindow::applyAutomationOperations);
    connect(m_automationController, &AutomationController::statusMessageRequested, this,
            [this](const QString& message, int timeoutMs) {
                if (timeoutMs > 0) {
                    statusBar()->showMessage(message, timeoutMs);
                }
                else {
                    statusBar()->showMessage(message);
                }
            });
    connect(m_automationShortcutController, &AutomationShortcutController::scriptTriggered, this,
            [this](const QString& scriptId) {
                if (m_automationController != nullptr) {
                    m_automationController->runScriptById(scriptId);
                }
            });
    connect(m_automationShortcutController, &AutomationShortcutController::missingScriptTriggered, this,
            [this](const QString& scriptId) {
                if (m_automationController != nullptr) {
                    m_automationController->showMissingScriptMessage(scriptId);
                }
            });
    m_projectViewController = new ProjectViewController(this);
    m_imagePageViewController = new ImagePageViewController(this);
    m_imagePageViewController->setCache(&m_imagePageCache);
    m_imagePageViewController->setProjectProvider([this]() -> const labelqt::core::Project& { return project(); });
    m_imagePageViewController->setCurrentImageIndexProvider([this]() { return m_currentImageIndex; });
    m_imagePageViewController->setPreviewTargetSizeProvider([this]() { return imagePreviewTargetSize(); });
    connect(m_imagePageViewController, &ImagePageViewController::viewRestored, this, [this](int zoomPercent) {
        if (m_zoomSlider == nullptr) {
            return;
        }
        const QSignalBlocker zoomBlocker(m_zoomSlider);
        m_zoomSlider->setValue(zoomPercent);
    });
    m_canvasTextEditController = new CanvasLabelTextEditController(this);
    m_canvasTextEditController->setCommitShortcut(m_preferences.commitLabelTextShortcut());
    m_canvasTextEditController->setEditorOpacity(m_preferences.canvasLabelTextEditorOpacity());
    m_editorStateController = new EditorStateController(this);
    m_editorStateController->setCallbacks([this]() { commitCanvasLabelTextEditor(); },
                                          [this]() { commitPendingTextEdit(); }, [this]() { editCurrentLabelText(); },
                                          [this]() { openCanvasLabelTextEditorForCurrentLabel(); });
    connect(m_canvasTextEditController, &CanvasLabelTextEditController::previewTextChanged, this,
            [this](int labelIndex, const QString& text) {
                if (m_canvas != nullptr) {
                    m_canvas->setLabelTextPreview(labelIndex, text);
                }
            });
    connect(m_canvasTextEditController, &CanvasLabelTextEditController::closed, this, [this](int labelIndex) {
        if (m_canvas != nullptr) {
            m_canvas->clearLabelTextPreview(labelIndex);
            m_canvas->setSelectedLabelTextBubblesVisible(true);
        }
    });
    connect(m_canvasTextEditController, &CanvasLabelTextEditController::textCommitted, this,
            [this](int imageIndex, int labelIndex, const QString& text) {
                if (m_labelEditController == nullptr || imageIndex < 0 || imageIndex >= project().images().size()) {
                    return;
                }
                const labelqt::core::ImageEntry& image = project().images().at(imageIndex);
                if (labelIndex < 0 || labelIndex >= image.labels.size() || image.labels.at(labelIndex).text() == text) {
                    return;
                }

                m_labelEditController->setLabelText(imageIndex, labelIndex, text);
                if (imageIndex == m_currentImageIndex) {
                    refreshCurrentLabelUi();
                    if (labelIndex == m_currentLabelIndex) {
                        selectLabel(labelIndex);
                    }
                }
            });
    m_labelTableMaxTextRows = m_preferences.labelTableMaxTextRows();
    m_labelTextDelegate->setCommitShortcut(m_preferences.commitLabelTextShortcut());
    m_undoStack.setChangedCallback([this]() { updateUndoRedoActions(); });
    m_projectWorkflowController =
        std::make_unique<labelqt::services::ProjectWorkflowController>(project(), m_undoStack, tr("Reorder pages"));
    m_projectWorkflowController->setCallbacks(
        [this](QVector<labelqt::core::ImageEntry> images, const QString& preferredImageName, int fallbackImageIndex,
               int zoomPercent, QPointF normalizedCenter, std::optional<QStringList> commentLines) {
            replaceProjectImages(std::move(images), preferredImageName, fallbackImageIndex, zoomPercent,
                                 normalizedCenter, std::move(commentLines));
        },
        [this]() { markDirty(); });
    m_labelEditController =
        std::make_unique<labelqt::services::LabelEditController>(project(), m_undoStack,
                                                                 labelqt::services::LabelEditCommandTexts{
                                                                     tr("Add label"),
                                                                     tr("Edit label text"),
                                                                     tr("Change label group"),
                                                                     tr("Move label"),
                                                                     tr("Delete labels"),
                                                                     tr("Reorder labels"),
                                                                     tr("Add group"),
                                                                     tr("Remove group"),
                                                                     tr("Add %1 %2 label %3"),
                                                                     tr("Delete %1 %2 label %3"),
                                                                     tr("Edit %1 %2 label %3"),
                                                                     tr("Change %1 %2 label %3"),
                                                                     tr("Move %1 %2 label %3"),
                                                                     tr("Reorder labels on %1"),
                                                                     tr("Add group %1"),
                                                                     tr("Remove group %1"),
                                                                 });
    m_labelEditController->setCallbacks(
        [this](int imageIndex, int labelIndex) { refreshLabelEditSelection(imageIndex, labelIndex); },
        [this](int imageIndex, QVector<int> labelIndexes) {
            refreshLabelEditSelection(imageIndex, std::move(labelIndexes));
        },
        [this](int imageIndex) { clearLabelEditSelection(imageIndex); },
        [this]() {
            refreshGroupUi();
            refreshCurrentLabelUi();
        },
        [this]() { markDirty(); });
    m_labelSelectionController = std::make_unique<LabelSelectionController>();

    setWindowTitle(QStringLiteral("LabelQt"));
    resize(1200, 800);

    createActions();
    createMenus();
    createCentralWidget();
    applyLabelTableFont();
    applyTextEditorFont();
    setEditorEnabled(false);
    m_operationMessageLabel = new QLabel(this);
    m_operationMessageLabel->setTextFormat(Qt::RichText);
    m_operationMessageLabel->setVisible(false);
    statusBar()->addWidget(m_operationMessageLabel, 1);
    m_warningLabel = new QLabel(this);
    m_warningLabel->setTextFormat(Qt::PlainText);
    m_warningLabel->setStyleSheet(QStringLiteral("QLabel { color: #b26a00; font-weight: 600; }"));
    m_warningLabel->setVisible(false);
    statusBar()->addPermanentWidget(m_warningLabel);
    statusBar()->showMessage(tr("Ready"));
    showPreferenceWarnings();
    restoreLayoutState();
    m_backupTimer = new QTimer(this);
    connect(m_backupTimer, &QTimer::timeout, this, &MainWindow::performAutoBackup);
    configureBackupTimer();
}

MainWindow::~MainWindow()
{
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
    if (m_labelView != nullptr) {
        m_labelView->removeEventFilter(this);
        if (m_labelView->viewport() != nullptr) {
            m_labelView->viewport()->removeEventFilter(this);
        }
    }
    if (m_textEdit != nullptr) {
        m_textEdit->removeEventFilter(this);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isAutomationRunning) {
        event->ignore();
        return;
    }

    commitActiveTextInput();
    if (!promptToSaveIfDirty()) {
        event->ignore();
        return;
    }

    saveProjectSessionState();
    saveLayoutState();
    saveLabelTableColumnWidths();
    event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_textEdit && event->type() == QEvent::FocusOut) {
        commitPendingTextEdit();
    }

    if (m_automationShortcutController != nullptr &&
        m_automationShortcutController->handleGlobalShortcut(watched, event)) {
        return true;
    }

    if (m_shortcutController != nullptr && m_shortcutController->handleGlobalShortcut(watched, event)) {
        return true;
    }

    if ((watched == m_labelView || (m_labelView != nullptr && watched == m_labelView->viewport())) &&
        m_shortcutController != nullptr && m_shortcutController->handleLabelViewShortcut(event)) {
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createActions()
{
    m_newProjectAction = new QAction(tr("&New Project..."), this);
    m_newProjectAction->setShortcut(QKeySequence::New);
    connect(m_newProjectAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openProjectAction = new QAction(tr("&Open LabelPlus Text..."), this);
    m_openProjectAction->setShortcut(QKeySequence::Open);
    connect(m_openProjectAction, &QAction::triggered, this, &MainWindow::openProject);

    m_mergeProjectsAction = new QAction(tr("&Merge Projects..."), this);
    connect(m_mergeProjectsAction, &QAction::triggered, this, &MainWindow::mergeProjects);

    m_reorderPagesAction = new QAction(tr("Reorder &Pages..."), this);
    connect(m_reorderPagesAction, &QAction::triggered, this, &MainWindow::reorderPages);

    m_saveProjectAction = new QAction(tr("&Save"), this);
    m_saveProjectAction->setShortcut(QKeySequence::Save);
    connect(m_saveProjectAction, &QAction::triggered, this, &MainWindow::saveProject);

    m_saveProjectAsAction = new QAction(tr("Save &As..."), this);
    m_saveProjectAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveProjectAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);

    m_undoAction = new QAction(tr("&Undo"), this);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoLastOperation);

    m_redoAction = new QAction(tr("&Redo"), this);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoLastOperation);

    m_previousPageAction = new QAction(tr("Previous page"), this);
    m_previousPageAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_previousPageAction, &QAction::triggered, this, &MainWindow::selectPreviousPage);
    addAction(m_previousPageAction);

    m_nextPageAction = new QAction(tr("Next page"), this);
    m_nextPageAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::selectNextPage);
    addAction(m_nextPageAction);

    m_preferencesAction = new QAction(tr("&Preferences..."), this);
    m_preferencesAction->setShortcut(QKeySequence::Preferences);
    connect(m_preferencesAction, &QAction::triggered, this, &MainWindow::openPreferences);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    updateEditShortcuts();
    updateUndoRedoActions();
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newProjectAction);
    fileMenu->addAction(m_openProjectAction);
    fileMenu->addAction(m_mergeProjectsAction);
    m_recentProjectsMenu = fileMenu->addMenu(tr("Recent Projects"));
    updateRecentProjectsMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveProjectAction);
    fileMenu->addAction(m_saveProjectAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_preferencesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_reorderPagesAction);

    QMenu* automationMenu = menuBar()->addMenu(tr("&Automation"));
    if (m_automationController != nullptr) {
        m_automationController->setMenu(automationMenu);
        m_automationController->refreshScripts();
    }
}

void MainWindow::createCentralWidget()
{
    m_rootSplitter = new QSplitter(Qt::Horizontal, this);
    m_rootSplitter->setChildrenCollapsible(false);

    auto* leftPanel = new QWidget(m_rootSplitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(6);

    m_canvas = new ImageCanvas(leftPanel);
    m_canvas->setPreferences(m_preferences);
    m_imagePageViewController->setCanvas(m_canvas);
    leftLayout->addWidget(m_canvas, 1);

    auto* bottomBar = new QWidget(leftPanel);
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(6);

    m_zoomSlider = new QSlider(Qt::Horizontal, bottomBar);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(180);
    bottomLayout->addWidget(new QLabel(tr("Zoom"), bottomBar));
    bottomLayout->addWidget(m_zoomSlider);
    bottomLayout->addStretch();

    auto* interactionModeGroup = new QButtonGroup(bottomBar);
    interactionModeGroup->setExclusive(true);
    m_labelModeButton = new QToolButton(bottomBar);
    m_selectionModeButton = new QToolButton(bottomBar);
    m_labelModeButton->setText(tr("Label mode"));
    m_selectionModeButton->setText(tr("Selection mode"));
    m_labelModeButton->setCheckable(true);
    m_selectionModeButton->setCheckable(true);
    m_labelModeButton->setChecked(true);
    m_labelModeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_selectionModeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    interactionModeGroup->addButton(m_labelModeButton);
    interactionModeGroup->addButton(m_selectionModeButton);
    bottomLayout->addWidget(m_labelModeButton);
    bottomLayout->addWidget(m_selectionModeButton);
    bottomLayout->addStretch();

    m_insertGroupComboBox = new QComboBox(bottomBar);
    m_insertGroupComboBox->setMinimumWidth(120);
    auto* addGroupButton = new QToolButton(bottomBar);
    auto* removeGroupButton = new QToolButton(bottomBar);
    addGroupButton->setIcon(
        QIcon::fromTheme(QStringLiteral("document-new"), style()->standardIcon(QStyle::SP_FileIcon)));
    removeGroupButton->setIcon(
        QIcon::fromTheme(QStringLiteral("user-trash"), style()->standardIcon(QStyle::SP_TrashIcon)));
    addGroupButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    removeGroupButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addGroupButton->setToolTip(tr("Add group"));
    removeGroupButton->setToolTip(tr("Remove selected group"));
    addGroupButton->setAutoRaise(true);
    removeGroupButton->setAutoRaise(true);
    addGroupButton->setFixedSize(24, 24);
    removeGroupButton->setFixedSize(24, 24);
    bottomLayout->addWidget(new QLabel(tr("Insert group"), bottomBar));
    bottomLayout->addWidget(m_insertGroupComboBox);
    bottomLayout->addWidget(addGroupButton);
    bottomLayout->addWidget(removeGroupButton);
    bottomLayout->addStretch();

    m_previousButton = new QPushButton(tr("Previous"), bottomBar);
    m_nextButton = new QPushButton(tr("Next"), bottomBar);
    m_imageComboBox = new PageSelectorComboBox(bottomBar);
    m_imageComboBox->setMinimumWidth(160);
    m_pageSourceLabel = new QLabel(bottomBar);
    m_pageSourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pageSourceLabel->setVisible(false);
    m_projectViewController->setWidgets(m_imageComboBox, m_previousButton, m_nextButton, m_pageSourceLabel);
    bottomLayout->addWidget(m_pageSourceLabel);
    bottomLayout->addWidget(m_previousButton);
    bottomLayout->addWidget(m_imageComboBox);
    bottomLayout->addWidget(m_nextButton);
    leftLayout->addWidget(bottomBar);

    auto* rightPanel = new QWidget(m_rootSplitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(8, 8, 8, 8);
    rightLayout->setSpacing(6);

    auto* groupBar = new QWidget(rightPanel);
    auto* groupLayout = new QHBoxLayout(groupBar);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(6);
    m_groupFilterComboBox = new GroupFilterComboBox(groupBar);
    groupLayout->addWidget(new QLabel(tr("Group"), groupBar));
    groupLayout->addWidget(m_groupFilterComboBox, 1);
    rightLayout->addWidget(groupBar);

    m_rightSplitter = new QSplitter(Qt::Vertical, rightPanel);
    m_rightSplitter->setChildrenCollapsible(false);

    m_labelView = new QTableView(m_rightSplitter);
    m_defaultLabelTableFont = m_labelView->font();
    m_labelView->setModel(m_labelModel);
    m_labelView->setItemDelegateForColumn(1, m_labelTextDelegate);
    m_labelView->setItemDelegateForColumn(2, m_labelGroupDelegate);
    m_labelView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_labelView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_labelView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_labelView->setDragEnabled(true);
    m_labelView->setAcceptDrops(true);
    m_labelView->setDropIndicatorShown(true);
    m_labelView->setDragDropMode(QAbstractItemView::InternalMove);
    m_labelView->setDragDropOverwriteMode(false);
    m_labelView->setDefaultDropAction(Qt::MoveAction);
    m_labelView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_labelView->setWordWrap(true);
    m_labelView->verticalHeader()->setVisible(false);
    m_labelView->setColumnWidth(LabelTableModel::NumberColumn, defaultLabelNumberColumnWidth);
    m_labelView->setColumnWidth(LabelTableModel::TextColumn, defaultLabelTextColumnWidth);
    m_labelView->setColumnWidth(LabelTableModel::GroupColumn, defaultLabelGroupColumnWidth);
    restoreLabelTableColumnWidths();
    m_labelTableColumns = new ViewportFittedTableColumns(
        m_labelView,
        {
            {LabelTableModel::NumberColumn, defaultLabelNumberColumnWidth, minimumLabelNumberColumnWidth, false},
            {LabelTableModel::TextColumn, defaultLabelTextColumnWidth, minimumLabelTextColumnWidth, true},
            {LabelTableModel::GroupColumn, defaultLabelGroupColumnWidth, minimumLabelGroupColumnWidth, false},
        },
        this);
    m_labelTableColumns->setColumnsChangedCallback([this]() { resizeLabelRowsToContents(); });
    m_labelTableColumns->fitToViewport();
    m_labelView->setAlternatingRowColors(true);
    m_labelView->installEventFilter(this);
    m_labelView->viewport()->installEventFilter(this);
    auto* labelRowsResizeTimer = new QTimer(m_labelView);
    labelRowsResizeTimer->setSingleShot(true);
    labelRowsResizeTimer->setInterval(0);
    connect(labelRowsResizeTimer, &QTimer::timeout, this, &MainWindow::resizeLabelRowsToContents);
    connect(m_labelView->horizontalHeader(), &QHeaderView::sectionResized, labelRowsResizeTimer,
            [labelRowsResizeTimer]() { labelRowsResizeTimer->start(); });
    connect(m_labelView->horizontalHeader(), &QHeaderView::sectionResized, m_labelTableColumns,
            &ViewportFittedTableColumns::rebalanceFromSectionResize);

    auto* editorPanel = new QWidget(m_rightSplitter);
    auto* editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(6);

    auto* labelGroupBar = new QWidget(editorPanel);
    auto* labelGroupLayout = new QHBoxLayout(labelGroupBar);
    labelGroupLayout->setContentsMargins(0, 0, 0, 0);
    labelGroupLayout->setSpacing(6);
    m_labelGroupComboBox = new QComboBox(labelGroupBar);
    labelGroupLayout->addWidget(new QLabel(tr("Label group"), labelGroupBar));
    labelGroupLayout->addWidget(m_labelGroupComboBox, 1);
    editorLayout->addWidget(labelGroupBar);

    m_textEdit = new QPlainTextEdit(editorPanel);
    m_defaultTextEditFont = m_textEdit->font();
    m_textEdit->setPlaceholderText(tr("Selected label text"));
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->installEventFilter(this);
    editorLayout->addWidget(m_textEdit, 1);
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 1);
    rightLayout->addWidget(m_rightSplitter, 1);

    m_rootSplitter->addWidget(leftPanel);
    m_rootSplitter->addWidget(rightPanel);
    m_rootSplitter->setStretchFactor(0, 3);
    m_rootSplitter->setStretchFactor(1, 2);
    setCentralWidget(m_rootSplitter);

    m_editorStateController->setWidgets(m_textEdit, m_labelView, m_canvasTextEditController);
    m_labelSelectionController->setWidgets(m_labelModel, m_labelView, m_canvas);
    m_labelSelectionController->setCallbacks([this](int labelIndex) { return updateCurrentLabelDetails(labelIndex); },
                                             [this]() {
                                                 m_currentLabelIndex = -1;
                                                 if (m_textEdit != nullptr) {
                                                     m_textEdit->clear();
                                                 }
                                                 resetPendingTextEdit();
                                                 setEditorEnabled(false);
                                             },
                                             [this](int row) { capLabelRowHeight(row); },
                                             [this]() { focusLabelTableSelection(); });

    connect(m_canvas, &ImageCanvas::labelCreateRequested, this, &MainWindow::addLabel);
    connect(m_canvas, &ImageCanvas::labelMoveRequested, this, &MainWindow::moveLabel);
    connect(m_canvas, &ImageCanvas::labelSelected, this, &MainWindow::selectLabel);
    connect(m_canvas, &ImageCanvas::labelClicked, this, &MainWindow::selectLabelFromCanvas);
    connect(m_canvas, &ImageCanvas::emptyAreaClicked, this, &MainWindow::clearCurrentLabelSelection);
    connect(m_canvas, &ImageCanvas::labelTextEditRequested, this, &MainWindow::openCanvasLabelTextEditor);
    connect(m_canvas, &ImageCanvas::deleteRequested, this, &MainWindow::deleteSelectedLabels);
    connect(m_canvas, &ImageCanvas::zoomPercentChanged, m_zoomSlider, &QSlider::setValue);
    connect(m_zoomSlider, &QSlider::valueChanged, m_canvas, &ImageCanvas::setZoomPercent);
    connect(m_imageComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::selectImage);
    connect(m_previousButton, &QPushButton::clicked, this, &MainWindow::selectPreviousPage);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::selectNextPage);
    connect(m_labelView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) {
                if (m_isTableEditorShortcutNavigation) {
                    return;
                }
                if (current.isValid()) {
                    updateCurrentLabelDetails(m_labelModel->sourceIndexForRow(current.row()));
                }
            });
    connect(m_labelView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (m_isUpdatingUi || m_isTableEditorShortcutNavigation || m_canvas == nullptr) {
            return;
        }
        const QVector<int> labelIndexes = selectedLabelIndexes();
        m_canvas->setSelectedLabels(labelIndexes);
        if (labelIndexes.isEmpty()) {
            clearCurrentLabelSelection();
        }
        else if (!labelIndexes.contains(m_currentLabelIndex)) {
            updateCurrentLabelDetails(labelIndexes.last());
        }
    });
    auto* deleteLabelShortcut = new QAction(m_labelView);
    deleteLabelShortcut->setShortcut(QKeySequence::Delete);
    deleteLabelShortcut->setShortcutContext(Qt::WidgetShortcut);
    m_labelView->addAction(deleteLabelShortcut);
    connect(deleteLabelShortcut, &QAction::triggered, this, &MainWindow::deleteSelectedLabels);
    connect(m_labelView, &QTableView::customContextMenuRequested, this, &MainWindow::showLabelContextMenu);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, &MainWindow::updateCurrentLabelText);
    connect(m_labelModel, &LabelTableModel::labelEditRequested, this, &MainWindow::updateLabelFromTable,
            Qt::QueuedConnection);
    connect(m_labelModel, &LabelTableModel::labelsReorderRequested, this, &MainWindow::reorderLabels);
    connect(m_labelTextDelegate, &LabelTextDelegate::editorHeightHintChanged, this,
            [this](const QPersistentModelIndex& index, QWidget* editor, int height) {
                if (!index.isValid() || m_labelView == nullptr || index.model() != m_labelModel ||
                    index.column() != LabelTableModel::TextColumn) {
                    return;
                }

                const int row = index.row();
                if (editor == nullptr || height <= 0) {
                    m_labelView->resizeRowToContents(row);
                    capLabelRowHeight(row);
                    return;
                }

                const int minimumHeight = m_labelView->verticalHeader()->minimumSectionSize();
                m_labelView->setRowHeight(row, std::max(minimumHeight, height));
                editor->setGeometry(m_labelView->visualRect(index));
            });
    connect(m_labelTextDelegate, &LabelTextDelegate::editorTextChanged, this,
            &MainWindow::previewLabelTextFromTableEditor);
    connect(m_labelTextDelegate, &LabelTextDelegate::editorTextPreviewFinished, this,
            &MainWindow::clearLabelTextPreviewFromTableEditor);
    connect(m_labelModeButton, &QToolButton::clicked, this,
            [this]() { m_canvas->setInteractionMode(ImageCanvas::InteractionMode::Label); });
    connect(m_selectionModeButton, &QToolButton::clicked, this, [this]() {
        commitActiveTextInput();
        m_canvas->setInteractionMode(ImageCanvas::InteractionMode::Selection);
    });
    connect(m_canvas, &ImageCanvas::imageCopiedToClipboard, this,
            [this]() { statusBar()->showMessage(tr("Image copied to clipboard."), 3000); });
    connect(m_groupFilterComboBox, &GroupFilterComboBox::selectedGroupsChanged, this, &MainWindow::updateGroupFilter);
    connect(m_labelGroupComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateCurrentLabelGroup);
    connect(m_insertGroupComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateInsertGroupTextColor);
    connect(addGroupButton, &QToolButton::clicked, this, &MainWindow::addGroup);
    connect(removeGroupButton, &QToolButton::clicked, this, &MainWindow::removeGroup);
}

void MainWindow::newProject()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitActiveTextInput();
    if (!promptToSaveIfDirty()) {
        return;
    }
    saveProjectSessionState();

    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, tr("Select image folder"), m_sessionStateStore.lastFileDialogDirectory(), QFileDialog::ShowDirsOnly);
    if (directoryPath.isEmpty()) {
        return;
    }
    m_sessionStateStore.saveLastFileDialogPath(directoryPath);

    const QString projectBaseName = tr("New Translation");
    const QStringList defaultGroups = defaultProjectGroups();
    labelqt::services::NewProjectResult result =
        m_projectController.createProjectFromImageDirectory(directoryPath, projectBaseName, defaultGroups, false);
    if (result.status == labelqt::services::NewProjectResult::Status::ProjectFileExists) {
        QMessageBox messageBox(QMessageBox::Question, tr("Project file already exists"),
                               tr("%1 already exists. Create the project with the next available name instead?")
                                   .arg(result.existingFileName),
                               QMessageBox::NoButton, this);
        QPushButton* tryAnotherNameButton = messageBox.addButton(tr("Try another name"), QMessageBox::AcceptRole);
        messageBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
        messageBox.exec();
        if (messageBox.clickedButton() != tryAnotherNameButton) {
            return;
        }

        result =
            m_projectController.createProjectFromImageDirectory(directoryPath, projectBaseName, defaultGroups, true);
    }

    if (result.status == labelqt::services::NewProjectResult::Status::NoImages) {
        QMessageBox::warning(this, tr("New project failed"), tr("No supported image files were found in this folder."));
        return;
    }
    if (result.status == labelqt::services::NewProjectResult::Status::Failed) {
        QMessageBox::critical(this, tr("New project failed"), result.error);
        return;
    }
    if (result.status == labelqt::services::NewProjectResult::Status::Created && openProjectFile(result.projectPath)) {
        statusBar()->showMessage(tr("Created %1").arg(result.projectPath), 4000);
    }
}

void MainWindow::openProject()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitActiveTextInput();
    if (!promptToSaveIfDirty()) {
        return;
    }
    saveProjectSessionState();

    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open LabelPlus text"), m_sessionStateStore.lastFileDialogDirectory(),
                                     tr("LabelPlus text (*.txt);;All files (*)"));

    if (path.isEmpty()) {
        return;
    }

    m_sessionStateStore.saveLastFileDialogPath(path);
    openProjectFile(path);
}

void MainWindow::mergeProjects()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Select LabelPlus projects to merge"),
                                                            m_sessionStateStore.lastFileDialogDirectory(),
                                                            tr("LabelPlus text (*.txt);;All files (*)"));
    if (paths.isEmpty()) {
        return;
    }
    m_sessionStateStore.saveLastFileDialogPath(paths.first());

    labelqt::services::ProjectMergePlan mergePlan;
    try {
        mergePlan = m_projectWorkflowController->createMergePlan(paths);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Merge failed"), QString::fromUtf8(error.what()));
        return;
    }

    if (mergePlan.mergedProject.images().isEmpty()) {
        QMessageBox::warning(this, tr("Merge Projects"), tr("No image pages were found in the selected projects."));
        return;
    }

    QVector<int> selectedCandidateIndexes;
    bool openMergedProjectAfterSave = m_sessionStateStore.shouldOpenMergedProjectAfterSave();
    if (mergePlan.conflicts.isEmpty()) {
        QMessageBox::information(this, tr("Merge Projects"),
                                 tr("No page conflicts were found. The selected projects can be merged directly."));
    }
    else {
        ProjectMergeDialog dialog(mergePlan, m_preferences, this);
        dialog.setShouldOpenMergedProjectAfterSave(openMergedProjectAfterSave);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        openMergedProjectAfterSave = dialog.shouldOpenMergedProjectAfterSave();
        m_sessionStateStore.saveShouldOpenMergedProjectAfterSave(openMergedProjectAfterSave);
        selectedCandidateIndexes = dialog.selectedCandidateIndexes();
    }

    const labelqt::core::Project orderPreviewProject =
        m_projectWorkflowController->mergedProjectPreview(mergePlan, selectedCandidateIndexes);
    PageOrderDialog pageOrderDialog(orderPreviewProject, m_preferences, this);
    if (pageOrderDialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString savePath = QFileDialog::getSaveFileName(this, tr("Save merged LabelPlus text"),
                                                          m_sessionStateStore.lastFileDialogDirectory(),
                                                          tr("LabelPlus text (*.txt);;All files (*)"));
    if (savePath.isEmpty()) {
        return;
    }
    m_sessionStateStore.saveLastFileDialogPath(savePath);

    if (!promptToSaveIfDirty()) {
        return;
    }
    saveProjectSessionState();

    labelqt::core::Project mergedProject = m_projectWorkflowController->mergedProject(
        std::move(mergePlan), selectedCandidateIndexes, savePath, pageOrderDialog.pageOrder());

    try {
        m_projectWorkflowController->saveProject(mergedProject, savePath);
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Merge failed"), QString::fromUtf8(error.what()));
        return;
    }

    if (openMergedProjectAfterSave) {
        if (!openProjectFile(savePath)) {
            statusBar()->showMessage(tr("Merged project saved to %1").arg(savePath), 4000);
        }
    }
    else {
        statusBar()->showMessage(tr("Merged project saved to %1").arg(savePath), 4000);
    }
}

void MainWindow::reorderPages()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitActiveTextInput();
    if (project().images().isEmpty()) {
        QMessageBox::information(this, tr("Reorder Pages"), tr("There are no pages to reorder."));
        return;
    }

    PageOrderDialog dialog(project(), m_preferences, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const labelqt::services::ProjectViewState viewState{
        m_currentImageIndex >= 0 && m_currentImageIndex < project().images().size()
            ? project().images().at(m_currentImageIndex).name
            : QString(),
        m_currentImageIndex,
        m_canvas == nullptr ? 100 : m_canvas->zoomPercent(),
        m_canvas == nullptr ? QPointF(0.5, 0.5) : m_canvas->normalizedViewCenter(),
    };
    m_projectWorkflowController->applyPageOrder(dialog.pageOrder(), viewState);
}

void MainWindow::showAutomationDiscoveryWarnings(const QStringList& warnings)
{
    if (warnings.isEmpty()) {
        return;
    }

    statusBar()->showMessage(
        tr("Skipped %n invalid automation script(s): %1", nullptr, static_cast<int>(warnings.size()))
            .arg(warnings.first()),
        8000);
}

void MainWindow::applyAutomationOperations(const QString& scriptName,
                                           const QVector<labelqt::services::AutomationOperation>& operations)
{
    const labelqt::services::AutomationOperationApplyPlan plan =
        labelqt::services::AutomationOperationApplier::plan(project(), operations);
    if (!plan.hasChanges()) {
        return;
    }

    auto applyChanges = [this, plan](bool redo) {
        labelqt::services::AutomationOperationApplier::apply(project(), plan, redo);
        refreshLabelViews();
        refreshCurrentLabelUi();
        markDirty();
    };

    applyChanges(true);
    m_undoStack.push(
        tr("Run automation script"), tr("Automation script %1").arg(scriptName),
        tr("Automation script %1").arg(scriptName), [applyChanges]() { applyChanges(false); },
        [applyChanges]() { applyChanges(true); });
    statusBar()->showMessage(tr("Automation applied %n change(s).", nullptr, static_cast<int>(plan.changeCount())),
                             5000);
}

bool MainWindow::openProjectFile(const QString& path)
{
    if (isProjectEditingBlocked()) {
        return false;
    }

    return loadProjectFile(path, true, tr("Loaded %1").arg(path));
}

bool MainWindow::openMostRecentProject()
{
    const QString path = m_sessionStateStore.mostRecentProjectPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (!path.isEmpty()) {
            m_sessionStateStore.removeRecentProjectPath(path);
            updateRecentProjectsMenu();
        }
        return false;
    }

    return loadProjectFile(path, false, tr("Loaded recent project: %1").arg(path));
}

bool MainWindow::loadProjectFile(const QString& path, bool showErrors, const QString& successMessage)
{
    try {
        detachProjectViewsFromProjectData();
        m_projectController.loadFromFile(path);
        m_undoStack.clear();
        refreshProjectUi();
        restoreProjectSessionState();
        m_sessionStateStore.addRecentProjectPath(project().filePath());
        updateRecentProjectsMenu();
        const QVector<labelqt::services::MissingProjectImage> missingImages =
            labelqt::services::ProjectImageValidator::missingImages(project());
        if (missingImages.isEmpty()) {
            statusBar()->showMessage(successMessage, 6000);
        }
        else {
            const int missingImageCount = static_cast<int>(missingImages.size());
            statusBar()->showMessage(tr("%n image file(s) are missing.", nullptr, missingImageCount), 8000);
        }
        return true;
    }
    catch (const std::exception& error) {
        m_sessionStateStore.removeRecentProjectPath(path);
        updateRecentProjectsMenu();
        if (showErrors) {
            QMessageBox::critical(this, tr("Open failed"), QString::fromUtf8(error.what()));
        }
        return false;
    }
}

void MainWindow::openRecentProjectFromAction()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const auto* action = qobject_cast<const QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    if (!promptToSaveIfDirty()) {
        return;
    }
    saveProjectSessionState();

    const QString path = action->data().toString();
    if (path.isEmpty()) {
        return;
    }

    if (!QFileInfo::exists(path)) {
        QTimer::singleShot(0, this, [this, path]() {
            m_sessionStateStore.removeRecentProjectPath(path);
            updateRecentProjectsMenu();
            QMessageBox::warning(this, tr("Open failed"), tr("%1 does not exist.").arg(path));
        });
        return;
    }

    QTimer::singleShot(0, this, [this, path]() { openProjectFile(path); });
}

void MainWindow::openPreferences()
{
    if (m_automationController != nullptr) {
        m_automationController->refreshScripts();
    }

    auto* dialog =
        new PreferenceDialog(labelqt::core::AppPreferences::defaultFilePath(), m_preferences,
                             m_automationController == nullptr ? QVector<labelqt::services::AutomationScript>{}
                                                               : m_automationController->scripts(),
                             this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &PreferenceDialog::preferencesApplied, this, &MainWindow::applyPreferences);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::saveProject()
{
    if (isProjectEditingBlocked()) {
        return false;
    }

    if (project().isEmpty()) {
        return true;
    }

    commitActiveTextInput();

    if (project().filePath().isEmpty()) {
        return saveProjectAs();
    }

    try {
        m_projectController.save();
        updateWindowTitle();
        statusBar()->showMessage(tr("Saved %1").arg(project().filePath()), 4000);
        return true;
    }
    catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::saveProjectAs()
{
    if (isProjectEditingBlocked()) {
        return false;
    }

    if (project().isEmpty()) {
        return true;
    }

    commitActiveTextInput();

    QString defaultSavePath = project().filePath();
    const QString lastDialogDirectory = m_sessionStateStore.lastFileDialogDirectory();
    if (!lastDialogDirectory.isEmpty()) {
        const QString fileName = QFileInfo(project().filePath()).fileName();
        defaultSavePath = fileName.isEmpty() ? lastDialogDirectory : QDir(lastDialogDirectory).filePath(fileName);
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Save LabelPlus text"), defaultSavePath,
                                                      tr("LabelPlus text (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return false;
    }
    m_sessionStateStore.saveLastFileDialogPath(path);

    const QString oldPath = project().filePath();
    try {
        m_projectController.saveAs(path);
        m_sessionStateStore.addRecentProjectPath(project().filePath());
        updateRecentProjectsMenu();
        updateWindowTitle();
        statusBar()->showMessage(tr("Saved %1").arg(project().filePath()), 4000);
        return true;
    }
    catch (const std::exception& error) {
        project().setFilePath(oldPath);
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::addGroup()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    bool ok = false;
    const QString group =
        QInputDialog::getText(this, tr("Add group"), tr("Group name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || group.isEmpty()) {
        return;
    }
    if (m_labelEditController == nullptr) {
        return;
    }

    const labelqt::services::LabelEditResult result = m_labelEditController->addGroup(group);
    if (!result.changed) {
        return;
    }

    refreshGroupUi();
    m_insertGroupComboBox->setCurrentText(group);
    refreshCurrentLabelUi();
}

void MainWindow::removeGroup()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const QString group = m_insertGroupComboBox->currentText();
    if (group.isEmpty() || project().groups().size() <= 1) {
        return;
    }

    QString fallback;
    for (const QString& candidate : project().groups()) {
        if (candidate != group) {
            fallback = candidate;
            break;
        }
    }
    if (fallback.isEmpty()) {
        return;
    }
    if (m_labelEditController == nullptr) {
        return;
    }

    const int labelCount = labelCountForGroup(group);
    if (labelCount > 0) {
        const QColor groupColor = colorForGroup(group);
        const QString coloredGroup = groupColor.isValid()
                                         ? QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                                               .arg(groupColor.name(), group.toHtmlEscaped())
                                         : group.toHtmlEscaped();
        const QString coloredFallback = colorForGroup(fallback).isValid()
                                            ? QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                                                  .arg(colorForGroup(fallback).name(), fallback.toHtmlEscaped())
                                            : fallback.toHtmlEscaped();
        QMessageBox confirmBox(this);
        confirmBox.setWindowTitle(tr("Remove group"));
        confirmBox.setTextFormat(Qt::RichText);
        confirmBox.setIcon(QMessageBox::Warning);
        confirmBox.setText(tr("Do you want to remove the %1 group? %n label(s) in this group will be moved to %2.",
                              nullptr, labelCount)
                               .arg(coloredGroup, coloredFallback));
        confirmBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirmBox.setDefaultButton(QMessageBox::No);
        if (confirmBox.exec() != QMessageBox::Yes) {
            return;
        }
    }

    const labelqt::services::LabelEditResult result = m_labelEditController->removeGroup(group, fallback);
    if (!result.changed) {
        return;
    }

    refreshGroupUi();
    m_insertGroupComboBox->setCurrentText(fallback);
    refreshCurrentLabelUi();
}

void MainWindow::updateGroupFilter(const QStringList& groups)
{
    if (m_isUpdatingUi) {
        return;
    }

    commitCanvasLabelTextEditor();
    m_labelModel->setGroupFilter(groups);
    m_canvas->setVisibleGroups(groups);
    resizeLabelRowsToContents();

    if (m_currentLabelIndex >= 0 && m_labelModel->rowForSourceIndex(m_currentLabelIndex) < 0) {
        clearCurrentLabelSelection();
    }
}

void MainWindow::selectImage(int index)
{
    if (m_isUpdatingUi || index < 0 || index >= project().images().size()) {
        return;
    }

    commitCanvasLabelTextEditor();
    commitPendingTextEdit();
    saveProjectSessionState();
    m_currentImageIndex = index;
    m_currentLabelIndex = -1;
    refreshImageUi();
}

void MainWindow::selectLabel(int index)
{
    const bool wasUpdatingUi = m_isUpdatingUi;
    m_isUpdatingUi = true;
    const bool selected = m_labelSelectionController != nullptr && m_labelSelectionController->selectSingle(index);
    setEditorEnabled(selected && !m_isAutomationRunning);
    m_isUpdatingUi = wasUpdatingUi;
}

void MainWindow::selectLabelFromCanvas(int index, Qt::KeyboardModifiers modifiers)
{
    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || index < 0 || index >= image->labels.size()) {
        clearCurrentLabelSelection();
        return;
    }

    if ((modifiers & Qt::ControlModifier) != Qt::NoModifier && (modifiers & Qt::ShiftModifier) == Qt::NoModifier) {
        QVector<int> sourceIndexes = selectedLabelIndexes();
        if (sourceIndexes.contains(index)) {
            sourceIndexes.removeAll(index);
        }
        else {
            sourceIndexes.append(index);
        }
        selectLabelIndexes(sourceIndexes, sourceIndexes.contains(index) ? index : -1);
        return;
    }

    if ((modifiers & Qt::ShiftModifier) == Qt::NoModifier || m_currentLabelIndex < 0 ||
        m_currentLabelIndex >= image->labels.size()) {
        selectLabel(index);
        return;
    }

    const int anchorRow = m_labelModel->rowForSourceIndex(m_currentLabelIndex);
    const int targetRow = m_labelModel->rowForSourceIndex(index);
    if (anchorRow < 0 || targetRow < 0) {
        selectLabel(index);
        return;
    }

    QVector<int> sourceIndexes;
    const int firstRow = std::min(anchorRow, targetRow);
    const int lastRow = std::max(anchorRow, targetRow);
    sourceIndexes.reserve(lastRow - firstRow + 1);
    for (int row = firstRow; row <= lastRow; ++row) {
        const int sourceIndex = m_labelModel->sourceIndexForRow(row);
        if (sourceIndex >= 0) {
            sourceIndexes.append(sourceIndex);
        }
    }
    selectLabelIndexes(sourceIndexes, index);
}

void MainWindow::addLabel(QPointF normalizedPosition)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    if (currentImage() == nullptr || m_labelEditController == nullptr) {
        return;
    }

    const QString group =
        m_insertGroupComboBox->currentText().isEmpty() ? QStringLiteral("框内") : m_insertGroupComboBox->currentText();
    if (!m_groupFilterComboBox->selectedGroups().contains(group)) {
        statusBar()->showMessage(tr("The insert group is hidden by the current filter."), 4000);
        return;
    }

    const labelqt::services::LabelEditResult result = m_labelEditController->addLabel(
        m_currentImageIndex, labelqt::core::Label(QString(), group, normalizedPosition));
    if (!result.changed) {
        return;
    }
    refreshLabelViews();
    selectLabel(result.selectedLabelIndex);
}

void MainWindow::deleteSelectedLabels()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const QVector<int> labelIndexes = selectedLabelIndexes();
    if (currentImage() == nullptr || labelIndexes.isEmpty() || m_labelEditController == nullptr) {
        return;
    }

    const labelqt::services::LabelEditResult result =
        m_labelEditController->deleteLabels(m_currentImageIndex, labelIndexes);
    if (!result.changed) {
        return;
    }

    refreshLabelViews();
    clearCurrentLabelSelection();
}

void MainWindow::changeSelectedLabelsGroup(const QString& group)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const QVector<int> labelIndexes = selectedLabelIndexes();
    if (currentImage() == nullptr || labelIndexes.isEmpty() || m_labelEditController == nullptr) {
        return;
    }

    const labelqt::services::LabelEditResult result =
        m_labelEditController->changeLabelsGroup(m_currentImageIndex, labelIndexes, group);
    if (!result.changed) {
        return;
    }

    refreshLabelViews();
    if (m_labelModel->rowForSourceIndex(result.selectedLabelIndex) >= 0) {
        selectLabel(result.selectedLabelIndex);
    }
    else {
        clearCurrentLabelSelection();
    }
}

void MainWindow::showLabelContextMenu(const QPoint& position)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    const QModelIndex clickedIndex = m_labelView->indexAt(position);
    if (clickedIndex.isValid() && !m_labelView->selectionModel()->isSelected(clickedIndex)) {
        m_labelView->selectRow(clickedIndex.row());
    }

    const QVector<int> labelIndexes = selectedLabelIndexes();
    if (labelIndexes.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction* deleteAction = menu.addAction(tr("Delete selected labels"));
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedLabels);
    menu.addSeparator();

    for (const QString& group : project().groups()) {
        auto* groupAction = new QWidgetAction(&menu);
        auto* groupButton = new QPushButton(group, &menu);
        groupButton->setFlat(true);
        groupButton->setCursor(Qt::PointingHandCursor);
        groupButton->setMinimumWidth(180);
        groupButton->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 4px 18px; border: none; }"
                                                  "QPushButton:hover { background: palette(highlight); }"));
        const QColor color = colorForGroup(group);
        if (color.isValid()) {
            groupButton->setStyleSheet(QStringLiteral("QPushButton { color: %1; text-align: left; padding: 4px 18px; "
                                                      "border: none; }"
                                                      "QPushButton:hover { background: palette(highlight); }")
                                           .arg(color.name()));
        }
        groupAction->setDefaultWidget(groupButton);
        menu.addAction(groupAction);
        connect(groupButton, &QPushButton::clicked, &menu, [this, &menu, group]() {
            menu.close();
            changeSelectedLabelsGroup(group);
        });
    }

    menu.exec(m_labelView->viewport()->mapToGlobal(position));
}

void MainWindow::reorderLabels(QVector<int> sourceIndexes, int visibleDropRow)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || sourceIndexes.isEmpty() || m_labelEditController == nullptr) {
        return;
    }

    const int visibleRowCount = m_labelModel->rowCount();
    visibleDropRow = std::clamp(visibleDropRow, 0, visibleRowCount);
    int insertBeforeSourceIndex = static_cast<int>(image->labels.size());
    if (visibleDropRow < visibleRowCount) {
        insertBeforeSourceIndex = m_labelModel->sourceIndexForRow(visibleDropRow);
        if (insertBeforeSourceIndex < 0) {
            return;
        }
    }

    const labelqt::services::LabelEditResult result =
        m_labelEditController->reorderLabels(m_currentImageIndex, sourceIndexes, insertBeforeSourceIndex);
    if (!result.changed) {
        return;
    }

    refreshCurrentLabelUi();
    selectLabelIndexes(result.selectedLabelIndexes);
}

void MainWindow::updateCurrentLabelText()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    if (m_isUpdatingUi) {
        return;
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || m_currentLabelIndex < 0 || m_currentLabelIndex >= image->labels.size()) {
        return;
    }

    const QString newText = m_textEdit->toPlainText();
    if (image->labels.at(m_currentLabelIndex).text() == newText) {
        return;
    }

    if (m_pendingTextEditImageIndex < 0 || m_pendingTextEditLabelIndex < 0) {
        m_pendingTextEditImageIndex = m_currentImageIndex;
        m_pendingTextEditLabelIndex = m_currentLabelIndex;
        m_pendingTextEditOldText = image->labels.at(m_currentLabelIndex).text();
    }

    if (m_labelEditController != nullptr) {
        m_labelEditController->setLabelText(m_currentImageIndex, m_currentLabelIndex, newText, false);
    }
    refreshCanvasLabels();
    m_labelModel->labelChanged(m_currentLabelIndex);
    const int visibleRow = m_labelModel->rowForSourceIndex(m_currentLabelIndex);
    if (visibleRow >= 0) {
        m_labelView->resizeRowToContents(visibleRow);
        capLabelRowHeight(visibleRow);
    }
}

bool MainWindow::updateCurrentLabelDetails(int index)
{
    if (!m_isUpdatingUi && m_canvasTextEditController != nullptr && m_canvasTextEditController->labelIndex() >= 0 &&
        m_canvasTextEditController->labelIndex() != index) {
        commitCanvasLabelTextEditor();
    }

    if (!m_isUpdatingUi &&
        (m_pendingTextEditImageIndex != m_currentImageIndex || m_pendingTextEditLabelIndex != index)) {
        commitPendingTextEdit();
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || index < 0 || index >= image->labels.size()) {
        return false;
    }

    const bool wasUpdatingUi = m_isUpdatingUi;
    m_isUpdatingUi = true;
    m_currentLabelIndex = index;
    m_textEdit->setPlainText(image->labels.at(index).text());
    m_labelGroupComboBox->setCurrentText(image->labels.at(index).group());
    setEditorEnabled(!m_isAutomationRunning);
    m_isUpdatingUi = wasUpdatingUi;
    return true;
}

void MainWindow::commitActiveTextInput()
{
    if (m_editorStateController != nullptr) {
        m_editorStateController->commitActive();
    }
}

void MainWindow::commitPendingTextEdit()
{
    if (m_pendingTextEditImageIndex < 0 || m_pendingTextEditLabelIndex < 0 || m_labelEditController == nullptr) {
        return;
    }

    if (m_pendingTextEditImageIndex >= project().images().size()) {
        resetPendingTextEdit();
        return;
    }

    const labelqt::core::ImageEntry& image = project().images().at(m_pendingTextEditImageIndex);
    if (m_pendingTextEditLabelIndex >= image.labels.size()) {
        resetPendingTextEdit();
        return;
    }

    const QString currentText = image.labels.at(m_pendingTextEditLabelIndex).text();
    if (currentText != m_pendingTextEditOldText) {
        m_labelEditController->registerLabelTextUndo(m_pendingTextEditImageIndex, m_pendingTextEditLabelIndex,
                                                     m_pendingTextEditOldText, currentText);
    }
    resetPendingTextEdit();
}

void MainWindow::resetPendingTextEdit()
{
    m_pendingTextEditImageIndex = -1;
    m_pendingTextEditLabelIndex = -1;
    m_pendingTextEditOldText.clear();
}

void MainWindow::refreshLabelTableView()
{
    m_labelModel->refresh();
    resizeLabelRowsToContents();
}

void MainWindow::refreshLabelViews()
{
    refreshLabelTableView();
    refreshCanvasLabels();
}

void MainWindow::clearCurrentLabelSelection()
{
    if (m_labelSelectionController != nullptr) {
        m_labelSelectionController->clearSelection();
        return;
    }
    m_currentLabelIndex = -1;
    m_textEdit->clear();
    resetPendingTextEdit();
    setEditorEnabled(false);
}

void MainWindow::updateCurrentLabelGroup(int index)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    if (m_isUpdatingUi || index < 0) {
        return;
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || m_currentLabelIndex < 0 || m_currentLabelIndex >= image->labels.size()) {
        return;
    }

    const QString oldGroup = image->labels.at(m_currentLabelIndex).group();
    const QString newGroup = m_labelGroupComboBox->itemText(index);
    if (oldGroup == newGroup) {
        return;
    }

    if (m_labelEditController == nullptr) {
        return;
    }
    const labelqt::services::LabelEditResult result =
        m_labelEditController->setLabelGroup(m_currentImageIndex, m_currentLabelIndex, newGroup);
    if (!result.changed) {
        return;
    }
    refreshLabelViews();
    selectLabel(m_currentLabelIndex);
}

void MainWindow::updateLabelFromTable(int sourceIndex, int column, QVariant newValue)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitPendingTextEdit();

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || sourceIndex < 0 || sourceIndex >= image->labels.size()) {
        return;
    }

    if (column == LabelTableModel::TextColumn && m_labelEditController != nullptr) {
        if (m_canvas != nullptr) {
            m_canvas->clearLabelTextPreview(sourceIndex);
        }
        m_labelEditController->setLabelText(m_currentImageIndex, sourceIndex, newValue.toString());
        m_labelModel->labelChanged(sourceIndex);
        refreshCanvasLabels();
    }
    if (column == LabelTableModel::GroupColumn && m_labelEditController != nullptr) {
        m_labelEditController->setLabelGroup(m_currentImageIndex, sourceIndex, newValue.toString());
        refreshLabelViews();
    }

    const bool commitStillTargetsCurrentSelection = m_currentLabelIndex < 0 || sourceIndex == m_currentLabelIndex;
    if (commitStillTargetsCurrentSelection && sourceIndex != m_suppressedTableCommitSelectionSourceIndex) {
        selectLabel(sourceIndex);
    }
}

void MainWindow::previewLabelTextFromTableEditor(QPersistentModelIndex index, const QString& text)
{
    if (!index.isValid() || index.model() != m_labelModel || index.column() != LabelTableModel::TextColumn) {
        return;
    }

    if (m_canvas == nullptr) {
        return;
    }

    const int sourceIndex = m_labelModel->sourceIndexForRow(index.row());
    if (sourceIndex < 0) {
        return;
    }

    m_canvas->setLabelTextPreview(sourceIndex, text);
}

void MainWindow::clearLabelTextPreviewFromTableEditor(QPersistentModelIndex index)
{
    if (!index.isValid() || index.model() != m_labelModel || index.column() != LabelTableModel::TextColumn ||
        m_canvas == nullptr) {
        return;
    }

    const int sourceIndex = m_labelModel->sourceIndexForRow(index.row());
    if (sourceIndex >= 0) {
        m_canvas->clearLabelTextPreview(sourceIndex);
    }
}

void MainWindow::openCanvasLabelTextEditor(int index, QPoint globalPosition)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || m_canvas == nullptr || m_canvasTextEditController == nullptr ||
        m_labelEditController == nullptr || index < 0 || index >= image->labels.size() ||
        !isLabelVisibleByGroupFilter(image->labels.at(index))) {
        return;
    }

    commitCanvasLabelTextEditor();
    m_canvas->setSelectedLabelTextBubblesVisible(false);
    selectLabel(index);

    m_canvasTextEditController->open(m_canvas->viewport(), m_currentImageIndex, index, image->labels.at(index).text(),
                                     m_textEdit != nullptr ? m_textEdit->font() : font(), globalPosition);
}

void MainWindow::commitCanvasLabelTextEditor()
{
    if (m_canvasTextEditController != nullptr) {
        m_canvasTextEditController->commit();
    }
}

void MainWindow::cancelCanvasLabelTextEditor()
{
    if (m_canvasTextEditController != nullptr) {
        m_canvasTextEditController->cancel();
    }
}

void MainWindow::closeCanvasLabelTextEditor()
{
    if (m_canvasTextEditController != nullptr) {
        m_canvasTextEditController->close();
    }
}

void MainWindow::moveLabel(int index, QPointF normalizedPosition)
{
    if (isProjectEditingBlocked()) {
        return;
    }

    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || index < 0 || index >= image->labels.size()) {
        return;
    }

    if (m_labelEditController == nullptr) {
        return;
    }

    const labelqt::services::LabelEditResult result =
        m_labelEditController->setLabelPosition(m_currentImageIndex, index, normalizedPosition);
    if (!result.changed) {
        refreshCanvasLabels();
        selectLabel(index);
        return;
    }

    refreshCanvasLabels();
    selectLabel(index);
}

void MainWindow::undoLastOperation()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitCanvasLabelTextEditor();
    commitPendingTextEdit();
    if (!m_undoStack.canUndo()) {
        updateUndoRedoActions();
        return;
    }
    showUndoRedoMessage(tr("Undo:"), m_undoStack.undo());
}

void MainWindow::redoLastOperation()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    commitCanvasLabelTextEditor();
    commitPendingTextEdit();
    if (!m_undoStack.canRedo()) {
        updateUndoRedoActions();
        return;
    }
    showUndoRedoMessage(tr("Redo:"), m_undoStack.redo());
}

void MainWindow::updateEditShortcuts()
{
    if (m_undoAction != nullptr) {
        m_undoAction->setShortcut(m_preferences.undoShortcut());
    }
    if (m_redoAction != nullptr) {
        m_redoAction->setShortcut(m_preferences.redoShortcut());
    }
    if (m_previousPageAction != nullptr) {
        m_previousPageAction->setShortcut(m_preferences.previousPageShortcut());
    }
    if (m_nextPageAction != nullptr) {
        m_nextPageAction->setShortcut(m_preferences.nextPageShortcut());
    }
}

void MainWindow::updateUndoRedoActions()
{
    if (m_undoAction != nullptr) {
        m_undoAction->setEnabled(!m_isAutomationRunning && m_undoStack.canUndo());
    }
    if (m_redoAction != nullptr) {
        m_redoAction->setEnabled(!m_isAutomationRunning && m_undoStack.canRedo());
    }
}

void MainWindow::showUndoRedoMessage(const QString& prefix, const QString& message)
{
    if (m_operationMessageLabel == nullptr || message.isEmpty()) {
        return;
    }

    statusBar()->clearMessage();
    m_operationMessageLabel->setText(QStringLiteral("%1 %2").arg(prefix.toHtmlEscaped(), richUndoRedoMessage(message)));
    m_operationMessageLabel->setVisible(true);
    const int messageSerial = ++m_operationMessageSerial;
    QTimer::singleShot(5000, m_operationMessageLabel, [this, label = m_operationMessageLabel, messageSerial]() {
        if (label != nullptr && messageSerial == m_operationMessageSerial) {
            label->clear();
            label->setVisible(false);
        }
    });
}

QString MainWindow::richUndoRedoMessage(QString message) const
{
    QString richText = message.toHtmlEscaped();
    QStringList groups = project().groups();
    std::sort(groups.begin(), groups.end(),
              [](const QString& lhs, const QString& rhs) { return lhs.size() > rhs.size(); });

    for (const QString& group : groups) {
        const QColor color = colorForGroup(group);
        if (!color.isValid()) {
            continue;
        }

        const QString escapedGroup = group.toHtmlEscaped();
        richText.replace(escapedGroup, QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                                           .arg(color.name(QColor::HexRgb), escapedGroup));
    }
    return richText;
}

void MainWindow::selectPreviousPage()
{
    if (m_currentImageIndex <= 0) {
        return;
    }

    selectImage(m_currentImageIndex - 1);
}

void MainWindow::selectNextPage()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= project().images().size() - 1) {
        return;
    }

    selectImage(m_currentImageIndex + 1);
}

void MainWindow::selectNextVisibleLabel()
{
    selectNextVisibleLabelFrom(m_currentImageIndex, m_currentLabelIndex);
}

void MainWindow::selectPreviousVisibleLabel()
{
    selectPreviousVisibleLabelFrom(m_currentImageIndex, m_currentLabelIndex);
}

void MainWindow::selectNextVisibleLabelFrom(int imageIndex, int labelIndex)
{
    if (project().isEmpty() || m_groupFilterComboBox == nullptr) {
        return;
    }

    const labelqt::services::LabelNavigationTarget target = labelqt::services::LabelNavigator::nextVisibleLabel(
        project(), {imageIndex, labelIndex, m_groupFilterComboBox->selectedGroups()});
    if (target.isValid()) {
        selectLabelAndCenter(target.imageIndex, target.labelIndex);
    }
}

void MainWindow::selectPreviousVisibleLabelFrom(int imageIndex, int labelIndex)
{
    if (project().isEmpty() || m_groupFilterComboBox == nullptr) {
        return;
    }

    const labelqt::services::LabelNavigationTarget target = labelqt::services::LabelNavigator::previousVisibleLabel(
        project(), {imageIndex, labelIndex, m_groupFilterComboBox->selectedGroups()});
    if (target.isValid()) {
        selectLabelAndCenter(target.imageIndex, target.labelIndex);
    }
}

void MainWindow::selectAdjacentVisibleLabelFromShortcut(bool previous)
{
    const EditorStateController::Mode textInputMode =
        m_editorStateController == nullptr ? EditorStateController::Mode::None : m_editorStateController->activeMode();
    const int navigationImageIndex = m_currentImageIndex;
    const int navigationLabelIndex = m_currentLabelIndex;
    const bool suppressTableCommitSelection = textInputMode == EditorStateController::Mode::TableTextEditor;

    if (suppressTableCommitSelection && !project().isEmpty() && m_groupFilterComboBox != nullptr) {
        const labelqt::services::LabelNavigationTarget target =
            previous ? labelqt::services::LabelNavigator::previousVisibleLabel(
                           project(), {navigationImageIndex, navigationLabelIndex, m_groupFilterComboBox->selectedGroups()})
                     : labelqt::services::LabelNavigator::nextVisibleLabel(
                           project(), {navigationImageIndex, navigationLabelIndex, m_groupFilterComboBox->selectedGroups()});
        if (!target.isValid()) {
            commitActiveTextInput();
            return;
        }

        m_suppressedTableCommitSelectionSourceIndex = navigationLabelIndex;
        m_isTableEditorShortcutNavigation = true;
        commitActiveTextInput();
        QTimer::singleShot(0, this, [this, target]() {
            if (target.imageIndex < 0 || target.imageIndex >= project().images().size()) {
                m_suppressedTableCommitSelectionSourceIndex = -1;
                m_isTableEditorShortcutNavigation = false;
                return;
            }
            const labelqt::core::ImageEntry& image = project().images().at(target.imageIndex);
            if (target.labelIndex < 0 || target.labelIndex >= image.labels.size() ||
                !isLabelVisibleByGroupFilter(image.labels.at(target.labelIndex))) {
                m_suppressedTableCommitSelectionSourceIndex = -1;
                m_isTableEditorShortcutNavigation = false;
                return;
            }
            selectLabelAndCenter(target.imageIndex, target.labelIndex);
            editCurrentLabelText();
            QTimer::singleShot(0, this, [this]() {
                m_suppressedTableCommitSelectionSourceIndex = -1;
                m_isTableEditorShortcutNavigation = false;
            });
        });
        return;
    }

    if (suppressTableCommitSelection) {
        m_suppressedTableCommitSelectionSourceIndex = navigationLabelIndex;
    }
    commitActiveTextInput();
    if (previous) {
        selectPreviousVisibleLabelFrom(navigationImageIndex, navigationLabelIndex);
    }
    else {
        selectNextVisibleLabelFrom(navigationImageIndex, navigationLabelIndex);
    }
    if (m_editorStateController != nullptr) {
        m_editorStateController->restoreAfterNavigation(textInputMode,
                                                        currentImage() != nullptr && m_currentLabelIndex >= 0);
    }
    if (suppressTableCommitSelection) {
        QTimer::singleShot(0, this, [this]() { m_suppressedTableCommitSelectionSourceIndex = -1; });
    }
}

void MainWindow::editCurrentLabelText()
{
    if (isProjectEditingBlocked()) {
        return;
    }

    if (m_labelView == nullptr || m_labelModel == nullptr || currentImage() == nullptr) {
        return;
    }

    int row = m_labelView->currentIndex().row();
    if (row < 0 && m_currentLabelIndex >= 0) {
        row = m_labelModel->rowForSourceIndex(m_currentLabelIndex);
    }
    if (row < 0) {
        return;
    }

    const QModelIndex textIndex = m_labelModel->index(row, LabelTableModel::TextColumn);
    if (!textIndex.isValid()) {
        return;
    }

    m_labelView->setCurrentIndex(textIndex);
    m_labelView->edit(textIndex);
}

void MainWindow::openCanvasLabelTextEditorForCurrentLabel()
{
    if (m_canvas == nullptr || m_currentLabelIndex < 0) {
        return;
    }

    openCanvasLabelTextEditor(m_currentLabelIndex, m_canvas->globalPositionForLabel(m_currentLabelIndex));
}

void MainWindow::selectLabelAndCenter(int imageIndex, int labelIndex)
{
    if (imageIndex < 0 || imageIndex >= project().images().size()) {
        return;
    }

    if (imageIndex != m_currentImageIndex) {
        selectImage(imageIndex);
    }

    selectLabel(labelIndex);
    if (m_canvas != nullptr) {
        m_canvas->centerOnLabel(labelIndex);
    }
}

bool MainWindow::isLabelVisibleByGroupFilter(const labelqt::core::Label& label) const
{
    return !label.isDeleted() && m_groupFilterComboBox != nullptr &&
           m_groupFilterComboBox->selectedGroups().contains(label.group());
}

QVector<int> MainWindow::selectedLabelIndexes() const
{
    return m_labelSelectionController == nullptr ? QVector<int>{} : m_labelSelectionController->selectedLabelIndexes();
}

void MainWindow::selectLabelIndexes(const QVector<int>& sourceIndexes, int primarySourceIndex)
{
    labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr || m_labelSelectionController == nullptr) {
        return;
    }

    const bool wasUpdatingUi = m_isUpdatingUi;
    m_isUpdatingUi = true;
    m_labelSelectionController->selectIndexes(*image, sourceIndexes, primarySourceIndex);
    m_isUpdatingUi = wasUpdatingUi;
}

void MainWindow::refreshProjectUi()
{
    m_isUpdatingUi = true;
    if (m_projectViewController != nullptr) {
        m_projectViewController->refreshProject(project());
    }
    refreshGroupUi();
    m_isUpdatingUi = false;
    m_labelModel->setGroupFilter(project().groups());

    m_currentImageIndex = project().images().isEmpty() ? -1 : 0;
    refreshImageUi();
    updateWindowTitle();
}

void MainWindow::detachProjectViewsFromProjectData()
{
    closeCanvasLabelTextEditor();
    resetPendingTextEdit();
    m_isUpdatingUi = true;
    if (m_labelView != nullptr && m_labelView->selectionModel() != nullptr) {
        m_labelView->selectionModel()->clear();
    }
    if (m_labelModel != nullptr) {
        m_labelModel->setLabels(nullptr);
    }
    if (m_canvas != nullptr) {
        m_canvas->setLabels({});
        m_canvas->setSelectedLabels({});
    }
    if (m_projectViewController != nullptr) {
        m_projectViewController->clear();
    }
    m_currentLabelIndex = -1;
    m_isUpdatingUi = false;
}

void MainWindow::replaceProjectImages(QVector<labelqt::core::ImageEntry> images, const QString& preferredImageName,
                                      int fallbackImageIndex, int zoomPercent, QPointF normalizedCenter,
                                      std::optional<QStringList> commentLines)
{
    detachProjectViewsFromProjectData();

    project().images() = std::move(images);
    if (commentLines.has_value()) {
        project().setCommentLines(std::move(*commentLines));
    }
    refreshProjectUi();

    int restoredImageIndex = -1;
    if (!preferredImageName.isEmpty()) {
        for (int i = 0; i < project().images().size(); ++i) {
            if (project().images().at(i).name == preferredImageName) {
                restoredImageIndex = i;
                break;
            }
        }
    }
    if (restoredImageIndex < 0 && !project().images().isEmpty()) {
        restoredImageIndex = std::clamp(fallbackImageIndex, 0, static_cast<int>(project().images().size()) - 1);
    }

    if (restoredImageIndex >= 0) {
        m_currentImageIndex = restoredImageIndex;
        refreshImageUi();
        if (m_imagePageViewController != nullptr) {
            m_imagePageViewController->restoreViewAfterCurrentImageDisplayed(zoomPercent, normalizedCenter);
        }
    }
}

void MainWindow::refreshImageUi()
{
    closeCanvasLabelTextEditor();
    m_isUpdatingUi = true;
    const labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr) {
        m_labelModel->setLabels(nullptr);
        m_imagePageViewController->clear();
        if (m_projectViewController != nullptr) {
            m_projectViewController->refreshCurrentPage(project(), m_currentImageIndex);
        }
        m_textEdit->clear();
        resetPendingTextEdit();
        setEditorEnabled(false);
        m_isUpdatingUi = false;
        return;
    }

    if (m_projectViewController != nullptr) {
        m_projectViewController->refreshCurrentPage(project(), m_currentImageIndex);
    }
    if (m_canvas != nullptr) {
        m_canvas->setSelectedLabels({});
    }
    m_imagePageViewController->displayCurrentImage();
    m_labelModel->setLabels(&project().images()[m_currentImageIndex].labels);
    resizeLabelRowsToContents();
    m_textEdit->clear();
    resetPendingTextEdit();
    setEditorEnabled(false);
    m_isUpdatingUi = false;
}

QSize MainWindow::imagePreviewTargetSize() const
{
    return m_canvas == nullptr ? QSize() : m_canvas->viewport()->size();
}

void MainWindow::refreshCanvasLabels()
{
    const labelqt::core::ImageEntry* image = currentImage();
    if (image == nullptr) {
        return;
    }

    m_imagePageViewController->refreshCurrentLabels();
}

void MainWindow::refreshCurrentLabelUi()
{
    if (currentImage() == nullptr) {
        return;
    }

    refreshLabelViews();
}

void MainWindow::refreshLabelEditSelection(int imageIndex, int labelIndex)
{
    if (imageIndex != m_currentImageIndex) {
        m_currentImageIndex = imageIndex;
        refreshImageUi();
    }
    else {
        refreshCurrentLabelUi();
    }

    if (m_labelModel->rowForSourceIndex(labelIndex) >= 0) {
        selectLabel(labelIndex);
    }
    else {
        clearCurrentLabelSelection();
    }
}

void MainWindow::refreshLabelEditSelection(int imageIndex, QVector<int> labelIndexes)
{
    if (imageIndex != m_currentImageIndex) {
        m_currentImageIndex = imageIndex;
        refreshImageUi();
    }
    else {
        refreshCurrentLabelUi();
    }
    selectLabelIndexes(labelIndexes);
}

void MainWindow::clearLabelEditSelection(int imageIndex)
{
    if (imageIndex != m_currentImageIndex) {
        m_currentImageIndex = imageIndex;
        refreshImageUi();
    }
    else {
        refreshCurrentLabelUi();
    }

    clearCurrentLabelSelection();
}

void MainWindow::refreshGroupUi()
{
    m_isUpdatingUi = true;
    const QString previousInsertGroup = m_insertGroupComboBox->currentText();
    m_labelGroupComboBox->clear();
    m_insertGroupComboBox->clear();
    if (project().groups().isEmpty()) {
        project().setGroups(defaultProjectGroups());
    }
    m_groupFilterComboBox->setGroups(project().groups(), m_preferences.groupStyles());
    m_canvas->setGroups(project().groups());
    m_canvas->setVisibleGroups(m_groupFilterComboBox->selectedGroups());
    m_labelModel->setGroups(project().groups(), m_preferences.groupStyles());
    m_labelGroupDelegate->setGroups(project().groups(), m_preferences.groupStyles());
    m_labelGroupComboBox->addItems(project().groups());
    m_insertGroupComboBox->addItems(project().groups());
    applyGroupStylesToCombo(m_insertGroupComboBox);
    if (!previousInsertGroup.isEmpty()) {
        m_insertGroupComboBox->setCurrentText(previousInsertGroup);
    }
    updateInsertGroupTextColor();
    m_isUpdatingUi = false;
}

void MainWindow::resizeLabelRowsToContents()
{
    if (m_labelView == nullptr) {
        return;
    }

    m_labelView->resizeRowsToContents();
    for (int row = 0; row < m_labelModel->rowCount(); ++row) {
        capLabelRowHeight(row);
    }
}

void MainWindow::capLabelRowHeight(int row)
{
    if (m_labelView == nullptr || row < 0 || row >= m_labelModel->rowCount()) {
        return;
    }

    const QFontMetrics metrics(m_labelView->font());
    const int contentHeight = metrics.lineSpacing() * std::max(1, m_labelTableMaxTextRows);
    const int verticalMargin = 10;
    const int maximumHeight =
        std::max(m_labelView->verticalHeader()->minimumSectionSize(), contentHeight + verticalMargin);
    if (m_labelView->rowHeight(row) > maximumHeight) {
        m_labelView->setRowHeight(row, maximumHeight);
    }
}

void MainWindow::focusLabelTableSelection()
{
    if (m_labelView != nullptr && m_labelView->isEnabled()) {
        m_labelView->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::restoreLayoutState()
{
    const labelqt::services::WindowLayoutState state = m_sessionStateStore.loadWindowLayout();

    if (!state.geometry.isEmpty()) {
        restoreGeometry(state.geometry);
    }

    if (!state.windowState.isEmpty()) {
        restoreState(state.windowState);
    }

    if (!state.rootSplitterState.isEmpty() && m_rootSplitter != nullptr) {
        m_rootSplitter->restoreState(state.rootSplitterState);
    }

    if (!state.rightSplitterState.isEmpty() && m_rightSplitter != nullptr) {
        m_rightSplitter->restoreState(state.rightSplitterState);
    }
}

void MainWindow::saveLayoutState() const
{
    labelqt::services::WindowLayoutState state;
    state.geometry = saveGeometry();
    state.windowState = saveState();
    if (m_rootSplitter != nullptr) {
        state.rootSplitterState = m_rootSplitter->saveState();
    }
    if (m_rightSplitter != nullptr) {
        state.rightSplitterState = m_rightSplitter->saveState();
    }
    m_sessionStateStore.saveWindowLayout(state);
}

void MainWindow::restoreLabelTableColumnWidths()
{
    if (m_labelView == nullptr || m_labelView->horizontalHeader() == nullptr) {
        return;
    }

    QHeaderView* header = m_labelView->horizontalHeader();
    const QSignalBlocker blocker(header);
    header->resizeSection(LabelTableModel::NumberColumn,
                          std::max(minimumLabelNumberColumnWidth,
                                   m_sessionStateStore.labelTableNumberColumnWidth(defaultLabelNumberColumnWidth)));
    header->resizeSection(LabelTableModel::GroupColumn,
                          std::max(minimumLabelGroupColumnWidth,
                                   m_sessionStateStore.labelTableGroupColumnWidth(defaultLabelGroupColumnWidth)));
}

void MainWindow::saveLabelTableColumnWidths() const
{
    if (m_labelView != nullptr && m_labelView->horizontalHeader() != nullptr) {
        m_sessionStateStore.saveLabelTableColumnWidths(m_labelView->columnWidth(LabelTableModel::NumberColumn),
                                                       m_labelView->columnWidth(LabelTableModel::GroupColumn));
    }
}

void MainWindow::restoreProjectSessionState()
{
    if (project().isEmpty() || project().filePath().isEmpty() || project().images().isEmpty()) {
        return;
    }

    const labelqt::services::ProjectSessionState state = m_sessionStateStore.loadProjectSession(project().filePath());
    if (!state.isValid) {
        return;
    }

    int imageIndex = state.imageIndex;
    if (!state.imageName.isEmpty()) {
        for (int i = 0; i < project().images().size(); ++i) {
            if (project().images().at(i).name == state.imageName) {
                imageIndex = i;
                break;
            }
        }
    }
    if (imageIndex < 0 || imageIndex >= project().images().size()) {
        imageIndex = 0;
    }

    m_currentImageIndex = imageIndex;
    m_currentLabelIndex = -1;
    refreshImageUi();
    if (m_imagePageViewController != nullptr) {
        m_imagePageViewController->restoreViewAfterCurrentImageDisplayed(state.zoomPercent, state.viewCenter);
    }

    const labelqt::core::ImageEntry* image = currentImage();
    QVector<int> selectedLabelIndexes;
    selectedLabelIndexes.reserve(state.selectedLabelIndexes.size());
    for (int labelIndex : state.selectedLabelIndexes) {
        if (image != nullptr && labelIndex >= 0 && labelIndex < image->labels.size() &&
            !image->labels.at(labelIndex).isDeleted() && m_labelModel->rowForSourceIndex(labelIndex) >= 0) {
            selectedLabelIndexes.append(labelIndex);
        }
    }
    if (!selectedLabelIndexes.isEmpty()) {
        const int primaryLabelIndex = selectedLabelIndexes.contains(state.selectedLabelIndex)
                                          ? state.selectedLabelIndex
                                          : selectedLabelIndexes.last();
        selectLabelIndexes(selectedLabelIndexes, primaryLabelIndex);
    }
}

void MainWindow::saveProjectSessionState() const
{
    if (project().isEmpty() || project().filePath().isEmpty() || m_currentImageIndex < 0 ||
        m_currentImageIndex >= project().images().size() || m_canvas == nullptr) {
        return;
    }

    labelqt::services::ProjectSessionState state;
    state.isValid = true;
    state.imageIndex = m_currentImageIndex;
    state.imageName = project().images().at(m_currentImageIndex).name;
    state.zoomPercent = m_canvas->zoomPercent();
    state.viewCenter = m_canvas->normalizedViewCenter();
    state.selectedLabelIndex = m_currentLabelIndex;
    state.selectedLabelIndexes = selectedLabelIndexes();
    m_sessionStateStore.saveProjectSession(project().filePath(), state);
}

void MainWindow::configureBackupTimer()
{
    if (m_backupTimer == nullptr) {
        return;
    }

    m_backupTimer->start(m_preferences.backupIntervalSeconds() * 1000);
}

void MainWindow::performAutoBackup()
{
    const labelqt::services::AutoBackupResult result = m_projectController.performAutoBackup(m_preferences);
    switch (result.status) {
    case labelqt::services::AutoBackupResult::Status::Skipped:
        return;
    case labelqt::services::AutoBackupResult::Status::Saved:
        statusBar()->showMessage(tr("Auto backed up %1").arg(result.path), 4000);
        return;
    case labelqt::services::AutoBackupResult::Status::Failed:
        if (QFileInfo(result.error).isAbsolute()) {
            statusBar()->showMessage(tr("Auto backup failed: could not create %1").arg(result.error), 4000);
        }
        else {
            statusBar()->showMessage(tr("Auto backup failed: %1").arg(result.error), 4000);
        }
        return;
    }
}

void MainWindow::applyLabelTableFont()
{
    if (m_labelView == nullptr) {
        return;
    }

    QFont font = m_defaultLabelTableFont;
    if (!m_preferences.labelTableFontFamily().isEmpty()) {
        font.setFamily(m_preferences.labelTableFontFamily());
    }
    if (m_preferences.labelTableFontPointSize() > 0.0) {
        font.setPointSizeF(m_preferences.labelTableFontPointSize());
    }

    m_labelView->setFont(font);
    m_labelView->viewport()->setFont(font);
    resizeLabelRowsToContents();
}

void MainWindow::applyTextEditorFont()
{
    if (m_textEdit == nullptr) {
        return;
    }

    QFont font = m_defaultTextEditFont;
    if (!m_preferences.labelTextEditorFontFamily().isEmpty()) {
        font.setFamily(m_preferences.labelTextEditorFontFamily());
    }
    if (m_preferences.labelTextEditorFontPointSize() > 0.0) {
        font.setPointSizeF(m_preferences.labelTextEditorFontPointSize());
    }
    m_textEdit->setFont(font);
}

void MainWindow::showPreferenceWarnings()
{
    if (m_warningLabel == nullptr) {
        return;
    }

    if (m_preferenceWarnings.isEmpty()) {
        m_warningLabel->clear();
        m_warningLabel->setToolTip({});
        m_warningLabel->setVisible(false);
        return;
    }

    QStringList messages;
    messages.reserve(m_preferenceWarnings.size());
    for (const labelqt::core::AppPreferenceWarning& warning : m_preferenceWarnings) {
        messages.append(preferenceWarningText(warning));
    }

    m_warningLabel->setText(tr("Preference warnings"));
    m_warningLabel->setToolTip(messages.join(QStringLiteral("\n")));
    m_warningLabel->setVisible(true);
}

void MainWindow::applyPreferences(labelqt::core::AppPreferencesLoadResult result)
{
    const bool languageChanged = m_preferences.applicationLanguage() != result.preferences.applicationLanguage();
    m_preferences = result.preferences;
    m_preferenceWarnings = std::move(result.warnings);
    m_labelTableMaxTextRows = m_preferences.labelTableMaxTextRows();
    m_labelTextDelegate->setCommitShortcut(m_preferences.commitLabelTextShortcut());
    if (m_shortcutController != nullptr) {
        m_shortcutController->setPreferences(m_preferences);
    }
    if (m_automationShortcutController != nullptr) {
        m_automationShortcutController->setPreferences(m_preferences);
    }
    if (m_automationController != nullptr) {
        m_automationController->setPreferences(m_preferences);
    }
    if (m_canvasTextEditController != nullptr) {
        m_canvasTextEditController->setCommitShortcut(m_preferences.commitLabelTextShortcut());
        m_canvasTextEditController->setEditorOpacity(m_preferences.canvasLabelTextEditorOpacity());
    }

    const QString styleName = m_preferences.applicationStyle().isEmpty()
                                  ? qApp->property("labelqt.defaultStyle").toString()
                                  : m_preferences.applicationStyle();
    if (!styleName.isEmpty() && QStyleFactory::keys().contains(styleName, Qt::CaseInsensitive)) {
        QApplication::setStyle(styleName);
    }
    if (!labelqt::ui::applyApplicationTheme(m_preferences.applicationTheme())) {
        m_preferenceWarnings.append(labelqt::core::AppPreferenceWarning{
            labelqt::core::AppPreferenceWarningType::AppearanceThemeWrongType,
            QStringLiteral("appearance.theme"),
            m_preferences.applicationTheme(),
        });
    }

    if (m_canvas != nullptr) {
        m_canvas->setPreferences(m_preferences);
    }

    refreshGroupUi();
    refreshCurrentLabelUi();
    applyLabelTableFont();
    applyTextEditorFont();
    updateEditShortcuts();
    configureBackupTimer();
    showPreferenceWarnings();
    if (languageChanged) {
        QMessageBox::information(this, tr("Language Changed"),
                                 tr("The language change will take effect after restarting the application."));
    }
    statusBar()->showMessage(tr("Preferences applied"), 4000);
}

QString MainWindow::preferenceWarningText(const labelqt::core::AppPreferenceWarning& warning) const
{
    using labelqt::core::AppPreferenceWarningType;

    switch (warning.type) {
    case AppPreferenceWarningType::FileNotReadable:
        return tr("Could not read preference.json; using default preferences.");
    case AppPreferenceWarningType::InvalidJson:
        return tr("preference.json is not valid JSON: %1; using default preferences.").arg(warning.detail);
    case AppPreferenceWarningType::RootNotObject:
        return tr("preference.json must contain a JSON object; using default preferences.");
    case AppPreferenceWarningType::AppearanceNotObject:
        return tr("appearance must be a JSON object; using default appearance preferences.");
    case AppPreferenceWarningType::AppearanceStyleWrongType:
        return tr("%1 must be a string; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::AppearanceThemeWrongType:
        return tr("%1 must name a built-in theme; using no application theme.").arg(warning.key);
    case AppPreferenceWarningType::AppearanceLanguageWrongType:
        return tr("%1 must be a string; using the system language.").arg(warning.key);
    case AppPreferenceWarningType::AutomationNotObject:
        return tr("automation must be a JSON object; using default automation preferences.");
    case AppPreferenceWarningType::AutomationShowRunLogWrongType:
        return tr("%1 must be true or false; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::AutomationPythonNotObject:
        return tr("%1 must be a JSON object; using default Python automation settings.").arg(warning.key);
    case AppPreferenceWarningType::AutomationPythonCommandWrongType:
        return tr("%1 must be a string; using automatic Python detection.").arg(warning.key);
    case AppPreferenceWarningType::AutomationPythonArgumentsNotArray:
        return tr("%1 must be an array; ignoring Python arguments.").arg(warning.key);
    case AppPreferenceWarningType::AutomationPythonArgumentWrongType:
        return tr("%1 entry %2 must be a string; ignoring this Python argument.")
            .arg(warning.key)
            .arg(warning.index + 1);
    case AppPreferenceWarningType::AutomationPythonAutoInstallRequirementsWrongType:
        return tr("%1 must be true or false; requirements will not be installed automatically.").arg(warning.key);
    case AppPreferenceWarningType::AutomationPythonPipIndexUrlWrongType:
        return tr("%1 must be a string; using the pip default index.").arg(warning.key);
    case AppPreferenceWarningType::AutomationShortcutsNotObject:
        return tr("%1 must be a JSON object; ignoring automation shortcuts.").arg(warning.key);
    case AppPreferenceWarningType::AutomationShortcutInvalid:
        return tr("%1 must be a valid shortcut string; ignoring this automation shortcut.").arg(warning.key);
    case AppPreferenceWarningType::LabelMarkerNotObject:
        return tr("labelMarker must be a JSON object; using default marker preferences.");
    case AppPreferenceWarningType::MarkerSizeWrongType:
        return tr("%1 must be a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerSizeOutOfRange:
        return tr("%1 must be a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTableNotObject:
        return tr("labelTable must be a JSON object; using default label table preferences.");
    case AppPreferenceWarningType::LabelTableMaxTextRowsWrongType:
        return tr("%1 must be a positive integer; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTableMaxTextRowsOutOfRange:
        return tr("%1 must be a positive integer; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTableFontFamilyWrongType:
        return tr("%1 must be a string; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTableFontPointSizeWrongType:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTableFontPointSizeOutOfRange:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTextEditorNotObject:
        return tr("labelTextEditor must be a JSON object; using default text editor preferences.");
    case AppPreferenceWarningType::LabelTextEditorFontFamilyWrongType:
        return tr("%1 must be a string; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTextEditorFontPointSizeWrongType:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::LabelTextEditorFontPointSizeOutOfRange:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerTextBubbleNotObject:
        return tr("markerTextBubble must be a JSON object; using default marker text bubble preferences.");
    case AppPreferenceWarningType::MarkerTextBubbleFontFamilyWrongType:
        return tr("%1 must be a string; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerTextBubbleFontPointSizeWrongType:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerTextBubbleFontPointSizeOutOfRange:
        return tr("%1 must be zero or a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerTextBubbleOpacityWrongType:
        return tr("%1 must be a number between 0 and 1; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::MarkerTextBubbleOpacityOutOfRange:
        return tr("%1 must be a number between 0 and 1; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::CanvasLabelTextEditorNotObject:
        return tr("canvasLabelTextEditor must be a JSON object; using default canvas label editor preferences.");
    case AppPreferenceWarningType::CanvasLabelTextEditorOpacityWrongType:
        return tr("%1 must be a number between 0 and 1; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::CanvasLabelTextEditorOpacityOutOfRange:
        return tr("%1 must be a number between 0 and 1; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::GroupStylesNotArray:
        return tr("groupStyles must be an array; group styles will use defaults.");
    case AppPreferenceWarningType::GroupStyleNotObject:
        return tr("groupStyles[%1] must be a JSON object; this group style will use defaults.").arg(warning.index);
    case AppPreferenceWarningType::InvalidGroupStyleColor:
        return tr("groupStyles[%1].groupColor is not a valid color; this color was skipped.").arg(warning.index);
    case AppPreferenceWarningType::GroupStyleMarkerSizeWrongType:
        return tr("%1 must be a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::GroupStyleMarkerSizeOutOfRange:
        return tr("%1 must be a positive number; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::GroupStyleMarkerStyleInvalid:
        return tr("groupStyles[%1].markerStyle must be circle or square; using the default value.").arg(warning.index);
    case AppPreferenceWarningType::InputNotObject:
        return tr("input must be a JSON object; using default input preferences.");
    case AppPreferenceWarningType::MoveLabelModifierInvalid:
    case AppPreferenceWarningType::PreviousLabelModifierInvalid:
        return tr("%1 must be a modifier name or modifier combination; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::UndoShortcutInvalid:
    case AppPreferenceWarningType::RedoShortcutInvalid:
    case AppPreferenceWarningType::NextLabelShortcutInvalid:
    case AppPreferenceWarningType::AlternatePreviousLabelShortcutInvalid:
    case AppPreferenceWarningType::AlternateNextLabelShortcutInvalid:
    case AppPreferenceWarningType::PreviousPageShortcutInvalid:
    case AppPreferenceWarningType::NextPageShortcutInvalid:
    case AppPreferenceWarningType::EditLabelTextShortcutInvalid:
    case AppPreferenceWarningType::CommitLabelTextShortcutInvalid:
        return tr("%1 must be a valid key sequence; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::BackupPathWrongType:
        return tr("%1 must be a non-empty string; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::BackupIntervalWrongType:
        return tr("%1 must be a positive integer; using the default value.").arg(warning.key);
    case AppPreferenceWarningType::BackupIntervalOutOfRange:
        return tr("%1 must be a positive integer; using the default value.").arg(warning.key);
    }

    return tr("Unknown preference warning.");
}

QStringList MainWindow::defaultProjectGroups() const
{
    return {tr("Inside frame"), tr("Outside frame")};
}

void MainWindow::markDirty()
{
    if (!m_isUpdatingUi) {
        m_projectController.markDirty();
        updateWindowTitle();
    }
}

void MainWindow::setDirty(bool dirty)
{
    if (m_projectController.isDirty() == dirty) {
        return;
    }

    m_projectController.setDirty(dirty);
    updateWindowTitle();
}

bool MainWindow::promptToSaveIfDirty()
{
    commitActiveTextInput();
    if (!m_projectController.isDirty()) {
        return true;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this, tr("Unsaved changes"), tr("The current project has unsaved changes. Do you want to save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    if (result == QMessageBox::Save) {
        return saveProject();
    }

    return true;
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("LabelQt");
    if (!project().filePath().isEmpty()) {
        title += QStringLiteral(" - %1").arg(project().filePath());
    }
    if (m_projectController.isDirty()) {
        title += QStringLiteral(" *");
    }
    setWindowTitle(title);
}

void MainWindow::updateRecentProjectsMenu()
{
    if (m_recentProjectsMenu == nullptr) {
        return;
    }

    m_recentProjectsMenu->setEnabled(!m_isAutomationRunning);
    m_recentProjectsMenu->clear();
    const QStringList paths = m_sessionStateStore.recentProjectPaths();
    if (paths.isEmpty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction(tr("No recent projects"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : paths) {
        const QFileInfo fileInfo(path);
        QAction* action = m_recentProjectsMenu->addAction(path);
        action->setData(path);
        action->setToolTip(path);
        if (!fileInfo.exists()) {
            action->setText(tr("%1 (missing)").arg(action->text()));
        }
        connect(action, &QAction::triggered, this, &MainWindow::openRecentProjectFromAction);
    }
}

void MainWindow::applyGroupStylesToCombo(QComboBox* comboBox)
{
    for (int i = 0; i < comboBox->count(); ++i) {
        const QColor color = colorForGroup(comboBox->itemText(i));
        if (color.isValid()) {
            comboBox->setItemData(i, color, Qt::ForegroundRole);
        }
    }
}

void MainWindow::updateInsertGroupTextColor()
{
    const QColor color = colorForGroup(m_insertGroupComboBox->currentText());
    m_insertGroupComboBox->setStyleSheet(color.isValid() ? QStringLiteral("QComboBox { color: %1; }").arg(color.name())
                                                         : QString());
}

QColor MainWindow::colorForGroup(const QString& group) const
{
    const int index = static_cast<int>(project().groups().indexOf(group));
    if (index < 0 || index >= static_cast<int>(m_preferences.groupStyles().size())) {
        return {};
    }
    return m_preferences.groupStyles().at(index).groupColor;
}

int MainWindow::labelCountForGroup(const QString& group) const
{
    int count = 0;
    for (const labelqt::core::ImageEntry& image : project().images()) {
        for (const labelqt::core::Label& label : image.labels) {
            if (!label.isDeleted() && label.group() == group) {
                ++count;
            }
        }
    }
    return count;
}

void MainWindow::setEditorEnabled(bool enabled)
{
    m_textEdit->setEnabled(enabled);
    m_labelGroupComboBox->setEnabled(enabled);
}

void MainWindow::setAutomationRunning(bool running)
{
    m_isAutomationRunning = running;

    const bool hasProject = !project().isEmpty();
    const bool hasPreviousPage = m_currentImageIndex > 0;
    const bool hasNextPage = m_currentImageIndex >= 0 && m_currentImageIndex < project().images().size() - 1;

    setEditorEnabled(!running && hasProject);
    if (m_canvas != nullptr) {
        m_canvas->setReadOnly(running);
        m_canvas->setEnabled(hasProject);
    }
    if (m_labelView != nullptr) {
        m_labelView->setEnabled(!running);
    }
    if (m_insertGroupComboBox != nullptr) {
        m_insertGroupComboBox->setEnabled(!running && hasProject);
    }
    if (m_groupFilterComboBox != nullptr) {
        m_groupFilterComboBox->setEnabled(hasProject);
    }
    if (m_imageComboBox != nullptr) {
        m_imageComboBox->setEnabled(hasProject);
    }
    if (m_previousButton != nullptr) {
        m_previousButton->setEnabled(hasProject && hasPreviousPage);
    }
    if (m_nextButton != nullptr) {
        m_nextButton->setEnabled(hasProject && hasNextPage);
    }
    if (m_labelModeButton != nullptr) {
        m_labelModeButton->setEnabled(hasProject);
    }
    if (m_selectionModeButton != nullptr) {
        m_selectionModeButton->setEnabled(hasProject);
    }
    if (m_undoAction != nullptr) {
        m_undoAction->setEnabled(!running && m_undoStack.canUndo());
    }
    if (m_redoAction != nullptr) {
        m_redoAction->setEnabled(!running && m_undoStack.canRedo());
    }
    if (m_newProjectAction != nullptr) {
        m_newProjectAction->setEnabled(!running);
    }
    if (m_openProjectAction != nullptr) {
        m_openProjectAction->setEnabled(!running);
    }
    if (m_recentProjectsMenu != nullptr) {
        m_recentProjectsMenu->setEnabled(!running);
    }
    if (m_mergeProjectsAction != nullptr) {
        m_mergeProjectsAction->setEnabled(!running);
    }
    if (m_reorderPagesAction != nullptr) {
        m_reorderPagesAction->setEnabled(!running && hasProject);
    }
    if (m_saveProjectAction != nullptr) {
        m_saveProjectAction->setEnabled(!running && hasProject);
    }
    if (m_saveProjectAsAction != nullptr) {
        m_saveProjectAsAction->setEnabled(!running && hasProject);
    }
    if (m_previousPageAction != nullptr) {
        m_previousPageAction->setEnabled(hasProject && hasPreviousPage);
    }
    if (m_nextPageAction != nullptr) {
        m_nextPageAction->setEnabled(hasProject && hasNextPage);
    }
}

bool MainWindow::isProjectEditingBlocked() const noexcept
{
    return m_isAutomationRunning;
}

labelqt::core::Project& MainWindow::project() noexcept
{
    return m_projectController.project();
}

const labelqt::core::Project& MainWindow::project() const noexcept
{
    return m_projectController.project();
}

labelqt::core::ImageEntry* MainWindow::currentImage()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= project().images().size()) {
        return nullptr;
    }
    return &project().images()[m_currentImageIndex];
}

const labelqt::core::ImageEntry* MainWindow::currentImage() const
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= project().images().size()) {
        return nullptr;
    }
    return &project().images().at(m_currentImageIndex);
}
