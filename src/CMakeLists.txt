set(LABELQT_CORE_SOURCES
    core/ApplicationTheme.cpp
    core/AppPreferences.cpp
    core/CommandLineUtils.cpp
    core/CommandLineUtils.h
    core/Label.cpp
    core/LabelPlusDocument.cpp
    core/Project.cpp
    core/TranslationManager.cpp
    core/UndoStack.cpp
)

set(LABELQT_SERVICE_SOURCES
    services/ArchiveReader.cpp
    services/AutomationManifestParser.cpp
    services/AutomationOperationApplier.cpp
    services/AutomationPythonResolver.cpp
    services/AutomationService.cpp
    services/ImagePageCache.cpp
    services/ImagePageCache.h
    services/LabelEditController.cpp
    services/LabelNavigator.cpp
    services/PageSourceInfoService.cpp
    services/ProjectImageValidator.cpp
    services/ProjectPageOrderService.cpp
    services/ProjectMergeService.cpp
    services/ProjectController.cpp
    services/ProjectWorkflowController.cpp
    services/SessionStateStore.cpp
    services/SecretStore.cpp
)

file(GLOB LABELQT_TRANSLATION_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../translations/labelqt_*.ts"
)
list(SORT LABELQT_TRANSLATION_FILES)

function(labelqt_add_runtime_files target_name)
    add_custom_target(${target_name}RuntimeFiles
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../preference.json"
            "$<TARGET_FILE_DIR:${target_name}>/preference.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/scripts"
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../scripts/official"
            "-DDEST_DIR=$<TARGET_FILE_DIR:${target_name}>/scripts/official"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SyncAutomationScripts.cmake"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${target_name}>/scripts/custom"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/scripts/custom"
        COMMENT "Syncing runtime files for ${target_name}"
        VERBATIM
    )
    add_dependencies(${target_name} ${target_name}RuntimeFiles)
endfunction()

set(LABELQT_UI_SOURCES
    ui/AutomationController.cpp
    ui/AutomationParameterDialog.cpp
    ui/AutomationRunDialog.cpp
    ui/AutomationShortcutEditorWidget.cpp
    ui/AutomationShortcutController.cpp
    ui/CanvasLabelItems.cpp
    ui/CanvasLabelTextEditController.cpp
    ui/CanvasLabelTextEditor.cpp
    ui/DialogWindowUtils.cpp
    ui/DialogWindowUtils.h
    ui/EditorStateController.cpp
    ui/GroupFilterComboBox.cpp
    ui/GroupStyleEditorWidget.cpp
    ui/ImageCanvas.cpp
    ui/ImagePageViewController.cpp
    ui/LabelEditDelegates.cpp
    ui/LabelSelectionController.cpp
    ui/LabelTableModel.cpp
    ui/MainWindow.cpp
    ui/MainWindowShortcutController.cpp
    ui/PageOrderDialog.cpp
    ui/PageOrderListModel.cpp
    ui/PageSelectorComboBox.cpp
    ui/PreferenceDialog.cpp
    ui/PreferenceDialogWidgets.cpp
    ui/ProjectMergeDialog.cpp
    ui/ProjectViewController.cpp
    ui/ThemeManager.cpp
    ui/ViewportFittedTableColumns.cpp
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

target_link_libraries(LabelQt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6Keychain::Qt6Keychain)

if(TARGET Qt6::Svg)
    target_link_libraries(LabelQt PRIVATE Qt6::Svg)
endif()

if(LibArchive_FOUND)
    target_link_libraries(LabelQt PRIVATE LibArchive::LibArchive)
    target_compile_definitions(LabelQt PRIVATE LABELQT_HAS_LIBARCHIVE=1)
endif()

if(COMMAND qt_add_translations)
    qt_add_translations(LabelQt
        TS_FILES
            ${LABELQT_TRANSLATION_FILES}
        RESOURCE_PREFIX "/i18n"
    )
endif()

labelqt_add_runtime_files(LabelQt)

if(WIN32)
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

if(LABELQT_ENABLE_STATIC_QT_EXPERIMENT)
    if(NOT WIN32)
        message(FATAL_ERROR "LABELQT_ENABLE_STATIC_QT_EXPERIMENT is only supported on Windows.")
    endif()
    if(QT6_IS_SHARED_LIBS_BUILD)
        message(FATAL_ERROR
            "LABELQT_ENABLE_STATIC_QT_EXPERIMENT requires a static Qt build, but the current Qt package is shared. "
            "Install or build static Qt and point CMAKE_PREFIX_PATH/Qt6_DIR to that static Qt before using the "
            "windows-static-release or windows-vs-static-release preset. A normal Qt installer package will still "
            "produce an executable that depends on Qt DLLs."
        )
    endif()

    qt_add_executable(LabelQtStatic
        WIN32
        main.cpp
        ${LABELQT_CORE_SOURCES}
        ${LABELQT_SERVICE_SOURCES}
        ${LABELQT_UI_SOURCES}
        ${LABELQT_RESOURCE_SOURCES}
    )

    target_include_directories(LabelQtStatic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(LabelQtStatic PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6Keychain::Qt6Keychain)

    if(TARGET Qt6::Svg)
        target_link_libraries(LabelQtStatic PRIVATE Qt6::Svg)
    endif()

    if(LibArchive_FOUND)
        target_link_libraries(LabelQtStatic PRIVATE LibArchive::LibArchive)
        target_compile_definitions(LabelQtStatic PRIVATE LABELQT_HAS_LIBARCHIVE=1)
    endif()

    if(COMMAND qt_add_translations)
        qt_add_translations(LabelQtStatic
            TS_FILES
                ${LABELQT_TRANSLATION_FILES}
            RESOURCE_PREFIX "/i18n"
        )
    endif()

    if(COMMAND qt_import_plugins AND TARGET Qt6::QWindowsIntegrationPlugin)
        qt_import_plugins(LabelQtStatic INCLUDE Qt6::QWindowsIntegrationPlugin)
    endif()

    if(MSVC)
        set_target_properties(LabelQtStatic PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    else()
        target_link_options(LabelQtStatic PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    labelqt_enable_warnings(LabelQtStatic)
    labelqt_add_runtime_files(LabelQtStatic)

    set_target_properties(LabelQtStatic PROPERTIES
        OUTPUT_NAME "labelqt-static"
    )
endif()

install(TARGETS LabelQt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../preference.json"
    DESTINATION bin
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION share/doc/labelqt
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/linux/org.labelqt.LabelQt.desktop"
    DESTINATION share/applications
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/linux/org.labelqt.LabelQt.svg"
    DESTINATION share/icons/hicolor/scalable/apps
)

install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/official"
    DESTINATION bin/scripts
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "*.pyo" EXCLUDE
    PATTERN "config.json" EXCLUDE
)

install(CODE "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/scripts/custom\")")
