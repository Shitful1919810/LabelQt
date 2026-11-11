if(LABELQT_BUILD_MOBILE AND NOT ANDROID)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test Quick QuickControls2)
    find_package(LibArchive REQUIRED)
    qt_add_executable(ProjectTransferTests ProjectTransferTests.cpp)
    target_compile_definitions(ProjectTransferTests PRIVATE LABELQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ProjectTransferTests PRIVATE LabelQtTransfer Qt6::Test LibArchive::LibArchive ZLIB::ZLIB)
    labelqt_enable_warnings(ProjectTransferTests)
    add_test(NAME ProjectTransferTests COMMAND $<TARGET_FILE:ProjectTransferTests>)
    qt_add_executable(MobileDocumentTests MobileDocumentTests.cpp)
    target_link_libraries(MobileDocumentTests PRIVATE LabelQtMobilePresentation Qt6::Test)
    labelqt_enable_warnings(MobileDocumentTests)
    add_test(NAME MobileDocumentTests COMMAND $<TARGET_FILE:MobileDocumentTests>)
    set_tests_properties(ProjectTransferTests MobileDocumentTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(MobileShellTests MobileShellTests.cpp
        ../src/platform/android/AndroidFileBridge.cpp ../src/platform/android/AndroidFileBridge.h)
    target_link_libraries(MobileShellTests PRIVATE LabelQtMobilePresentation Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(MobileShellTests PRIVATE LABELQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    labelqt_enable_warnings(MobileShellTests)
    add_test(NAME MobileShellTests COMMAND $<TARGET_FILE:MobileShellTests>)
    set_tests_properties(MobileShellTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    qt_add_executable(MobileCanvasTests MobileCanvasTests.cpp)
    target_link_libraries(MobileCanvasTests PRIVATE LabelQtMobilePresentation Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(MobileCanvasTests PRIVATE LABELQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    labelqt_enable_warnings(MobileCanvasTests)
    add_test(NAME MobileCanvasTests COMMAND $<TARGET_FILE:MobileCanvasTests>)
    set_tests_properties(MobileCanvasTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    qt_add_executable(MobileWorkspaceTests MobileWorkspaceTests.cpp)
    target_link_libraries(MobileWorkspaceTests PRIVATE LabelQtMobilePresentation Qt6::Test)
    labelqt_enable_warnings(MobileWorkspaceTests)
    add_test(NAME MobileWorkspaceTests COMMAND $<TARGET_FILE:MobileWorkspaceTests>)
    set_tests_properties(MobileWorkspaceTests PROPERTIES TIMEOUT 120)
endif()

if(NOT LABELQT_BUILD_DESKTOP)
    return()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

qt_add_resources(LABELQT_TEST_RESOURCE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/themes/breeze/breeze.qrc"
)

# Compile resources into the test executables so static archive extraction cannot omit them.
add_library(LabelQtTestSupport INTERFACE)
target_sources(LabelQtTestSupport INTERFACE ${LABELQT_TEST_RESOURCE_SOURCES})
target_link_libraries(LabelQtTestSupport INTERFACE LabelQtDesktop Qt6::Test)
if(TARGET Qt6::Svg)
    target_link_libraries(LabelQtTestSupport INTERFACE Qt6::Svg)
endif()

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
qt_add_executable(ExternalProjectChangeTests ExternalProjectChangeTests.cpp)
target_link_libraries(ExternalProjectChangeTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(ExternalProjectChangeTests)
add_test(NAME ExternalProjectChangeTests COMMAND $<TARGET_FILE:ExternalProjectChangeTests>)
set_tests_properties(ExternalProjectChangeTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
qt_add_executable(RiskRegressionTests RiskRegressionTests.cpp)
target_link_libraries(RiskRegressionTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(RiskRegressionTests)
add_test(NAME RiskRegressionTests COMMAND $<TARGET_FILE:RiskRegressionTests>)
set_tests_properties(RiskRegressionTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
target_link_libraries(PersistenceSafetyTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(PersistenceSafetyTests)
add_test(NAME PersistenceSafetyTests COMMAND $<TARGET_FILE:PersistenceSafetyTests>)
set_tests_properties(PersistenceSafetyTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ProductWorkflowTests ProductWorkflowTests.cpp)
target_link_libraries(ProductWorkflowTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(ProductWorkflowTests)
add_test(NAME ProductWorkflowTests COMMAND $<TARGET_FILE:ProductWorkflowTests>)
set_tests_properties(ProductWorkflowTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

foreach(test_name IN ITEMS PreferenceStoreTests RecordStorageTests ResourceUiTests UiInteractionTests ReviewRoundTests)
    qt_add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE LabelQtTestSupport)
    target_compile_definitions(${test_name} PRIVATE LABELQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    labelqt_enable_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endforeach()

add_test(NAME LabelQtTests COMMAND $<TARGET_FILE:LabelQtTests>)
set_tests_properties(LabelQtTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ProofreadChangeTableModelTests
    ProofreadChangeTableModelTests.cpp
)

target_link_libraries(ProofreadChangeTableModelTests PRIVATE LabelQtTestSupport)
labelqt_enable_warnings(ProofreadChangeTableModelTests)
add_test(NAME ProofreadChangeTableModelTests COMMAND $<TARGET_FILE:ProofreadChangeTableModelTests>)
set_tests_properties(ProofreadChangeTableModelTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
