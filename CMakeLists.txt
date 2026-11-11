cmake_minimum_required(VERSION 3.24)

project(LabelQt
    VERSION 0.1.0
    DESCRIPTION "Cross-platform image labeling and translation assistant"
    HOMEPAGE_URL "https://github.com/Shitful1919810/LabelQt"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for clangd")

if(ANDROID)
    set(LABELQT_DESKTOP_DEFAULT OFF)
    set(LABELQT_MOBILE_DEFAULT ON)
else()
    set(LABELQT_DESKTOP_DEFAULT ON)
    set(LABELQT_MOBILE_DEFAULT OFF)
endif()
option(LABELQT_BUILD_DESKTOP "Build the Qt Widgets desktop frontend" ${LABELQT_DESKTOP_DEFAULT})
option(LABELQT_BUILD_MOBILE "Build the Qt Quick mobile frontend" ${LABELQT_MOBILE_DEFAULT})
option(LABELQT_ENABLE_TESTS "Build tests" ON)
option(LABELQT_REQUIRE_QT_SVG "Fail configure when Qt Svg is unavailable" OFF)
option(LABELQT_REQUIRE_LINGUIST_TOOLS "Fail configure when Qt LinguistTools is unavailable" OFF)
option(LABELQT_ENABLE_WINDOWS_STATIC_RELEASE
    "Build the official Windows release target against a fully static Qt dependency set."
    OFF
)
option(LABELQT_ENABLE_EXPERIMENTAL_DEB_PACKAGE
    "Include the experimental DEB generator in the default CPack generator list."
    OFF
)
option(LABELQT_INSTALL_OFFICIAL_AUTOMATION_SCRIPTS
    "Install the bundled official automation scripts in native packages."
    ON
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(DiffMatchPatch)
include(ProjectWarnings)
include(GNUInstallDirs)

if(UNIX AND NOT APPLE AND NOT ANDROID AND LABELQT_BUILD_DESKTOP)
    include(CheckPIESupported)
    check_pie_supported()
endif()

labelqt_add_diff_match_patch(DiffMatchPatch)

if(LABELQT_ENABLE_WINDOWS_STATIC_RELEASE)
    if(NOT WIN32)
        message(FATAL_ERROR "LABELQT_ENABLE_WINDOWS_STATIC_RELEASE is only supported on Windows.")
    endif()
    if(QT6_IS_SHARED_LIBS_BUILD)
        message(FATAL_ERROR
            "LABELQT_ENABLE_WINDOWS_STATIC_RELEASE requires a static Qt build. Install or build static Qt and point "
            "CMAKE_PREFIX_PATH/Qt6_DIR to it before configuring a Windows static release preset."
        )
    endif()
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_target_properties(DiffMatchPatch PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endif()

add_subdirectory(src)

if(LABELQT_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    add_custom_target(LabelQtCompileCommands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json for clangd"
        VERBATIM
    )
endif()

include(Packaging)
