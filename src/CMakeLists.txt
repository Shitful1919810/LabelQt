set(LABELQT_CORE_SOURCES
    core/ApplicationTheme.cpp
    core/AppPreferences.cpp
    core/AtomicFileWriter.cpp
    core/CommandLineUtils.cpp
    core/CommandLineUtils.h
    core/Label.cpp
    core/LabelPlusDocument.cpp
    core/Project.cpp
    core/ProjectIdentityService.cpp
    core/ProjectMetadataService.cpp
    core/ProjectMetadataService.h
    core/RuntimePaths.cpp
    core/RuntimePaths.h
    core/TextDiffCleanupMode.h
    core/TranslationManager.cpp
    core/UndoStack.cpp
)

set(LABELQT_SERVICE_SOURCES
    services/automation/AutomationManifestParser.cpp
    services/automation/AutomationProtocol.cpp
    services/automation/ProjectTaskService.cpp
    services/automation/AutomationPythonResolver.cpp
    services/automation/AutomationService.cpp
    services/automation/SecretStore.cpp
    services/io/ImagePageCache.cpp
    services/io/FileRenameTransaction.cpp
    services/io/FileRevision.cpp
    services/io/ProjectFileTransaction.cpp
    services/io/ProjectFileTransaction.h
    services/io/ProjectFileMonitor.cpp
    services/io/ProjectFileMonitor.h
    services/io/ImagePageCache.h
    services/label/LabelClipboardService.cpp
    services/label/LabelClipboardService.h
    services/label/LabelEditController.cpp
    services/label/LabelEditControllerBatch.cpp
    services/label/ProjectTextSearchService.cpp
    services/label/LabelNavigator.cpp
    services/label/LabelPastePlanner.cpp
    services/label/LabelPastePlanner.h
    services/preferences/PreferenceStore.cpp
    services/project/PageSourceInfoService.cpp
    services/project/ProjectController.cpp
    services/project/ProjectRevisionTracker.cpp
    services/project/ProjectLocationService.cpp
    services/project/PageManagementDraft.cpp
    services/project/ProjectRecoveryService.cpp
    services/project/ProjectImageRenameService.cpp
    services/project/ProjectImageValidator.cpp
    services/project/ProjectMergeService.cpp
    services/project/ProjectPageOrderService.cpp
    services/project/ProjectWorkflowController.cpp
    services/project/SaveChangesDecision.cpp
    services/project/SaveChangesDecision.h
    services/project/SessionStateStore.cpp
    services/proofread/LabelSequenceDiffService.cpp
    services/proofread/LabelSequenceDiffService.h
    services/proofread/ProjectComparisonService.cpp
    services/proofread/ProjectComparisonService.h
    services/proofread/ProofreadReportService.cpp
    services/proofread/ProofreadReportService.h
    services/proofread/ReviewChangeClassifier.cpp
    services/proofread/ReviewChangeClassifier.h
    services/proofread/ReviewMetadataService.cpp
    services/proofread/ReviewSessionService.cpp
    services/proofread/ReviewRoundService.cpp
    services/proofread/ReviewRoundEditService.cpp
    services/proofread/ReviewMetadataService.h
    services/proofread/TextDiffHtmlRenderer.cpp
    services/proofread/TextDiffHtmlRenderer.h
    services/proofread/TextDiffService.cpp
    services/proofread/TextDiffService.h
)

file(GLOB LABELQT_TRANSLATION_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../translations/labelqt_*.ts"
)
list(SORT LABELQT_TRANSLATION_FILES)

set(LABELQT_UI_SOURCES
    ui/automation/AutomationController.cpp
    ui/automation/AutomationParameterDialog.cpp
    ui/automation/AutomationRunDialog.cpp
    ui/automation/AutomationShortcutController.cpp
    ui/automation/AutomationShortcutEditorWidget.cpp
    ui/canvas/CanvasBottomBar.cpp
    ui/canvas/CanvasLabelItems.cpp
    ui/canvas/CanvasLabelTextEditController.cpp
    ui/canvas/CanvasLabelTextEditor.cpp
    ui/canvas/ImageCanvas.cpp
    ui/canvas/ImagePageViewController.cpp
    ui/labeltable/LabelEditDelegates.cpp
    ui/labeltable/LabelSelectionController.cpp
    ui/labeltable/LabelTableModel.cpp
    ui/labeltable/LabelTableView.cpp
    ui/labeltable/CurrentPageLabelContext.cpp
    ui/labeltable/CurrentPageLabelContext.h
    ui/EditorStateController.cpp
    ui/preferences/GroupStyleEditorWidget.cpp
    ui/preferences/PreferenceDialog.cpp
    ui/preferences/PreferenceDialogWidgets.cpp
    ui/project/ImageRenamePreviewWidget.cpp
    ui/project/PageOrderDialog.cpp
    ui/project/PageOrderListModel.cpp
    ui/project/PageSelectorComboBox.cpp
    ui/project/ProjectMergeDialog.cpp
    ui/project/MergeCandidatePreview.cpp
    ui/project/ProjectViewController.cpp
    ui/project/ProjectRecoveryDialog.cpp
    ui/project/ProjectRecoveryController.cpp
    ui/project/ProjectExternalChangeController.cpp
    ui/search/ProjectSearchPanel.cpp
    ui/search/ProjectTextSearchModel.cpp
    ui/proofread/ProofreadChangesDialog.cpp
    ui/proofread/ProofreadReviewPanel.cpp
    ui/proofread/ProofreadNotesWidget.cpp
    ui/proofread/ReviewStatusDelegate.cpp
    ui/proofread/ProofreadChangeTableModel.cpp
    ui/proofread/ProofreadingController.cpp
    ui/widgets/CheckableFilterButton.cpp
    ui/widgets/BusyTaskDialog.cpp
    ui/widgets/BackgroundTask.h
    ui/widgets/BusyTaskDialog.h
    ui/widgets/DialogWindowUtils.cpp
    ui/widgets/DialogWindowUtils.h
    ui/widgets/GroupFilterComboBox.cpp
    ui/widgets/ThemeManager.cpp
    ui/labeltable/ViewportFittedTableColumns.cpp
    ui/MainWindow.cpp
    ui/MainWindowShortcutController.cpp
)

qt_add_resources(LABELQT_RESOURCE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/themes/breeze/breeze.qrc"
)

qt_add_executable(LabelQt
    WIN32
    MACOSX_BUNDLE
    main.cpp
    ${LABELQT_CORE_SOURCES}
    ${LABELQT_SERVICE_SOURCES}
    ${LABELQT_UI_SOURCES}
    ${LABELQT_RESOURCE_SOURCES}
)

target_include_directories(LabelQt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(LabelQt PRIVATE LABELQT_INSTALL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/labelqt")

target_link_libraries(LabelQt PRIVATE DiffMatchPatch Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6Keychain::Qt6Keychain ZLIB::ZLIB)

if(TARGET Qt6::Svg)
    target_link_libraries(LabelQt PRIVATE Qt6::Svg)
endif()

if(COMMAND qt_add_translations)
    qt_add_translations(LabelQt
        TS_FILES
            ${LABELQT_TRANSLATION_FILES}
        RESOURCE_PREFIX "/i18n"
    )
endif()

function(labelqt_add_runtime_files target_name runtime_target_name)
    add_custom_target(${runtime_target_name} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../preference.json"
            "$<TARGET_FILE_DIR:${target_name}>/preference.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/scripts"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${target_name}>/scripts/official"
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../scripts/official"
            "-DDEST_DIR=$<TARGET_FILE_DIR:${target_name}>/scripts/official"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SyncAutomationScripts.cmake"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/scripts/custom"
        COMMENT "Syncing runtime files for ${target_name}"
        VERBATIM
    )
    add_dependencies(${target_name} ${runtime_target_name})
endfunction()

labelqt_add_runtime_files(LabelQt LabelQtRuntimeFiles)

if(WIN32 AND NOT LABELQT_ENABLE_WINDOWS_STATIC_RELEASE)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_target(deploy_windows
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                "$<TARGET_FILE:LabelQt>"
            DEPENDS LabelQt LabelQtRuntimeFiles
            COMMENT "Deploying Qt runtime dependencies with windeployqt"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt was not found. Add the Qt bin directory to PATH to enable the deploy_windows target.")
    endif()
endif()

labelqt_enable_warnings(LabelQt)

set_target_properties(LabelQt PROPERTIES
    OUTPUT_NAME "labelqt"
    MACOSX_BUNDLE_BUNDLE_NAME "LabelQt"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.labelqt.LabelQt"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

if(UNIX AND NOT APPLE)
    set_target_properties(LabelQt PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INSTALL_RPATH ""
    )
endif()

if(LABELQT_ENABLE_WINDOWS_STATIC_RELEASE)
    set_target_properties(LabelQt PROPERTIES OUTPUT_NAME "labelqt-static")

    if(COMMAND qt_import_plugins AND TARGET Qt6::QWindowsIntegrationPlugin)
        qt_import_plugins(LabelQt INCLUDE Qt6::QWindowsIntegrationPlugin)
    endif()

    if(MSVC)
        set_target_properties(LabelQt PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    else()
        target_link_options(LabelQt PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

set(LABELQT_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/labelqt")
set(LABELQT_INSTALL_LICENSEDIR "${LABELQT_INSTALL_DOCDIR}/licenses")
set(LABELQT_INSTALL_APPDATADIR "${CMAKE_INSTALL_DATAROOTDIR}/labelqt")

install(TARGETS LabelQt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION "${LABELQT_INSTALL_DOCDIR}"
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/themes/breeze/LICENSE.md"
    DESTINATION "${LABELQT_INSTALL_LICENSEDIR}"
    RENAME BreezeStyleSheets-LICENSE.md
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/themes/breeze/MaterialUi.LICENSE"
    DESTINATION "${LABELQT_INSTALL_LICENSEDIR}"
    RENAME MaterialUi-LICENSE.txt
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/diff-match-patch/LICENSE"
    DESTINATION "${LABELQT_INSTALL_LICENSEDIR}"
    RENAME diff-match-patch-LICENSE.txt
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/linux/org.labelqt.LabelQt.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/linux/org.labelqt.LabelQt.svg"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
)

if(LABELQT_INSTALL_OFFICIAL_AUTOMATION_SCRIPTS)
    install(DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/official"
        DESTINATION "${LABELQT_INSTALL_APPDATADIR}/scripts"
        USE_SOURCE_PERMISSIONS
        PATTERN "test" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "*.pyo" EXCLUDE
        PATTERN "config.json" EXCLUDE
    )
endif()
