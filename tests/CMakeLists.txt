find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Test Widgets)

qt_add_executable(LabelQtTests
    LabelTests.cpp
    ../src/core/ApplicationTheme.cpp
    ../src/core/AppPreferences.cpp
    ../src/core/CommandLineUtils.cpp
    ../src/core/LabelPlusDocument.cpp
    ../src/core/Label.cpp
    ../src/core/Project.cpp
    ../src/core/ProjectMetadataService.cpp
    ../src/core/RuntimePaths.cpp
    ../src/core/UndoStack.cpp
    ../src/services/automation/AutomationManifestParser.cpp
    ../src/services/automation/AutomationOperationApplier.cpp
    ../src/services/automation/AutomationPythonResolver.cpp
    ../src/services/automation/AutomationService.cpp
    ../src/services/automation/SecretStore.cpp
    ../src/services/label/LabelClipboardService.cpp
    ../src/services/label/LabelEditController.cpp
    ../src/services/label/LabelNavigator.cpp
    ../src/services/label/LabelPastePlanner.cpp
    ../src/services/project/PageSourceInfoService.cpp
    ../src/services/project/ProjectController.cpp
    ../src/services/project/ProjectImageRenameService.cpp
    ../src/services/project/ProjectImageValidator.cpp
    ../src/services/project/ProjectMergeService.cpp
    ../src/services/project/ProjectPageOrderService.cpp
    ../src/services/project/SaveChangesDecision.cpp
    ../src/services/project/SessionStateStore.cpp
    ../src/services/proofread/LabelSequenceDiffService.cpp
    ../src/services/proofread/ProjectComparisonService.cpp
    ../src/services/proofread/ProofreadReportService.cpp
    ../src/services/proofread/ReviewChangeClassifier.cpp
    ../src/services/proofread/ReviewMetadataService.cpp
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
    ../src/ui/project/PageOrderListModel.cpp
)

target_include_directories(LabelQtTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(LabelQtTests
    PRIVATE
        DiffMatchPatch
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
        Qt6Keychain::Qt6Keychain
)

labelqt_enable_warnings(LabelQtTests)

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
