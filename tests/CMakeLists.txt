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
    ../src/services/AutomationManifestParser.cpp
    ../src/services/AutomationOperationApplier.cpp
    ../src/services/AutomationPythonResolver.cpp
    ../src/services/AutomationService.cpp
    ../src/services/LabelClipboardService.cpp
    ../src/services/LabelEditController.cpp
    ../src/services/LabelSequenceDiffService.cpp
    ../src/services/LabelPastePlanner.cpp
    ../src/services/LabelNavigator.cpp
    ../src/services/PageSourceInfoService.cpp
    ../src/services/ProjectComparisonService.cpp
    ../src/services/ProjectImageValidator.cpp
    ../src/services/ProjectController.cpp
    ../src/services/ProjectMergeService.cpp
    ../src/services/ProjectPageOrderService.cpp
    ../src/services/ProofreadReportService.cpp
    ../src/services/ReviewChangeClassifier.cpp
    ../src/services/ReviewMetadataService.cpp
    ../src/services/SaveChangesDecision.cpp
    ../src/services/SessionStateStore.cpp
    ../src/services/SecretStore.cpp
    ../src/services/TextDiffHtmlRenderer.cpp
    ../src/services/TextDiffService.cpp
    ../src/ui/CanvasLabelItems.cpp
    ../src/ui/CurrentPageLabelContext.cpp
    ../src/ui/ImageCanvas.cpp
    ../src/ui/LabelSelectionController.cpp
    ../src/ui/LabelTableModel.cpp
    ../src/ui/PageOrderListModel.cpp
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
