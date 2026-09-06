find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Test Widgets)

add_library(LabelQtTestSupport STATIC
    ../src/core/ApplicationTheme.cpp
    ../src/core/AppPreferences.cpp
    ../src/core/AtomicFileWriter.cpp
    ../src/core/CommandLineUtils.cpp
    ../src/core/LabelPlusDocument.cpp
    ../src/core/Label.cpp
    ../src/core/Project.cpp
    ../src/core/ProjectIdentityService.cpp
    ../src/core/ProjectMetadataService.cpp
    ../src/core/RuntimePaths.cpp
    ../src/core/UndoStack.cpp
    ../src/services/automation/AutomationManifestParser.cpp
    ../src/services/automation/AutomationProtocol.cpp
    ../src/services/automation/ProjectTaskService.cpp
    ../src/services/automation/AutomationPythonResolver.cpp
    ../src/services/automation/AutomationService.cpp
    ../src/services/automation/SecretStore.cpp
    ../src/services/io/FileRenameTransaction.cpp
    ../src/services/io/FileRevision.cpp
    ../src/services/label/LabelClipboardService.cpp
    ../src/services/label/LabelEditController.cpp
    ../src/services/label/LabelEditControllerBatch.cpp
    ../src/services/label/ProjectTextSearchService.cpp
    ../src/ui/search/ProjectTextSearchModel.cpp
    ../src/services/label/LabelNavigator.cpp
    ../src/services/label/LabelPastePlanner.cpp
    ../src/services/project/PageSourceInfoService.cpp
    ../src/services/project/ProjectController.cpp
    ../src/services/project/ProjectRevisionTracker.cpp
    ../src/services/project/ProjectLocationService.cpp
    ../src/services/project/PageManagementDraft.cpp
    ../src/services/project/ProjectRecoveryService.cpp
    ../src/services/project/ProjectImageRenameService.cpp
    ../src/services/project/ProjectImageValidator.cpp
    ../src/services/project/ProjectMergeService.cpp
    ../src/services/project/ProjectPageOrderService.cpp
    ../src/services/project/ProjectWorkflowController.cpp
    ../src/services/project/SaveChangesDecision.cpp
    ../src/services/project/SessionStateStore.cpp
    ../src/services/proofread/LabelSequenceDiffService.cpp
    ../src/services/proofread/ProjectComparisonService.cpp
    ../src/services/proofread/ProofreadReportService.cpp
    ../src/services/proofread/ReviewChangeClassifier.cpp
    ../src/services/proofread/ReviewMetadataService.cpp
    ../src/services/proofread/ReviewSessionService.cpp
    ../src/services/proofread/TextDiffHtmlRenderer.cpp
    ../src/services/proofread/TextDiffService.cpp
    ../src/ui/canvas/CanvasLabelItems.cpp
    ../src/ui/canvas/CanvasLabelTextEditController.cpp
    ../src/ui/canvas/CanvasLabelTextEditor.cpp
    ../src/ui/labeltable/CurrentPageLabelContext.cpp
    ../src/ui/canvas/ImageCanvas.cpp
    ../src/ui/labeltable/LabelEditDelegates.cpp
    ../src/ui/labeltable/LabelSelectionController.cpp
    ../src/ui/labeltable/LabelTableModel.cpp
    ../src/ui/labeltable/LabelTableView.cpp
    ../src/ui/project/PageOrderListModel.cpp
    ../src/ui/project/PageOrderDialog.cpp
    ../src/ui/project/ImageRenamePreviewWidget.cpp
    ../src/ui/project/ProjectRecoveryDialog.cpp
    ../src/ui/search/ProjectSearchPanel.cpp
    ../src/ui/proofread/ProofreadChangesDialog.cpp
    ../src/ui/proofread/ProofreadReviewPanel.cpp
    ../src/ui/proofread/ProofreadChangeTableModel.cpp
    ../src/ui/labeltable/ViewportFittedTableColumns.cpp
    ../src/ui/widgets/DialogWindowUtils.cpp
    ../src/ui/widgets/BusyTaskDialog.cpp
    ../src/ui/widgets/CheckableFilterButton.cpp
    ../src/ui/automation/AutomationRunDialog.cpp
)

target_include_directories(LabelQtTestSupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(LabelQtTestSupport
    PUBLIC
        DiffMatchPatch
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
        Qt6::Concurrent
        Qt6Keychain::Qt6Keychain
)

labelqt_enable_warnings(LabelQtTestSupport)

qt_add_executable(LabelQtTests LabelTests.cpp)
target_link_libraries(LabelQtTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(LabelQtTests)

qt_add_executable(AutomationTaskTests AutomationTaskTests.cpp)
target_link_libraries(AutomationTaskTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(AutomationTaskTests)
add_test(NAME AutomationTaskTests COMMAND $<TARGET_FILE:AutomationTaskTests>)

qt_add_executable(AutomationProtocolTests AutomationProtocolTests.cpp)
target_link_libraries(AutomationProtocolTests PRIVATE LabelQtTestSupport)
target_compile_definitions(AutomationProtocolTests PRIVATE LABELQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
labelqt_enable_warnings(AutomationProtocolTests)
add_test(NAME AutomationProtocolTests COMMAND $<TARGET_FILE:AutomationProtocolTests>)

qt_add_executable(PersistenceSafetyTests PersistenceSafetyTests.cpp)
target_link_libraries(PersistenceSafetyTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(PersistenceSafetyTests)
add_test(NAME PersistenceSafetyTests COMMAND $<TARGET_FILE:PersistenceSafetyTests>)
set_tests_properties(PersistenceSafetyTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ProductWorkflowTests ProductWorkflowTests.cpp)
target_link_libraries(ProductWorkflowTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(ProductWorkflowTests)
add_test(NAME ProductWorkflowTests COMMAND $<TARGET_FILE:ProductWorkflowTests>)
set_tests_properties(ProductWorkflowTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME LabelQtTests COMMAND $<TARGET_FILE:LabelQtTests>)
set_tests_properties(LabelQtTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ProofreadChangeTableModelTests
    ProofreadChangeTableModelTests.cpp
    ../src/services/proofread/ReviewChangeClassifier.cpp
    ../src/services/proofread/TextDiffService.cpp
    ../src/ui/proofread/ProofreadChangeTableModel.cpp
)

target_include_directories(ProofreadChangeTableModelTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(ProofreadChangeTableModelTests PRIVATE DiffMatchPatch Qt6::Core Qt6::Test)
labelqt_enable_warnings(ProofreadChangeTableModelTests)
add_test(NAME ProofreadChangeTableModelTests COMMAND $<TARGET_FILE:ProofreadChangeTableModelTests>)
