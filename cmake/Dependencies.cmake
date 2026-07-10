find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent OPTIONAL_COMPONENTS Svg)
find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS LinguistTools)
find_package(Qt6Keychain REQUIRED)

if(LABELQT_REQUIRE_QT_SVG AND NOT TARGET Qt6::Svg)
    message(FATAL_ERROR "Qt Svg is required for this release build but was not found.")
endif()
if(LABELQT_REQUIRE_LINGUIST_TOOLS AND NOT Qt6LinguistTools_FOUND)
    message(FATAL_ERROR "Qt LinguistTools is required for this release build but was not found.")
endif()

qt_standard_project_setup()
