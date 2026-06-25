cmake_minimum_required(VERSION 3.24)

project(LabelQt
    VERSION 0.1.0
    DESCRIPTION "Cross-platform image labeling and translation assistant"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for clangd")

option(LABELQT_ENABLE_TESTS "Build tests" ON)
option(LABELQT_REQUIRE_LIBARCHIVE "Fail configure when libarchive is unavailable" OFF)
option(LABELQT_ENABLE_STATIC_QT_EXPERIMENT
    "Enable the experimental Windows-only static Qt single-exe target for local builds. Do not use for official releases without Qt license review."
    OFF
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(ProjectWarnings)

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
