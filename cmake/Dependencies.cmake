find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent OPTIONAL_COMPONENTS Svg)
find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS LinguistTools)
find_package(Qt6Keychain REQUIRED)

qt_standard_project_setup()

find_package(LibArchive QUIET)
if(NOT LibArchive_FOUND AND LABELQT_REQUIRE_LIBARCHIVE)
    message(FATAL_ERROR "libarchive was not found. Install libarchive-dev/libarchive-devel or disable LABELQT_REQUIRE_LIBARCHIVE.")
endif()

if(LibArchive_FOUND)
    message(STATUS "libarchive found: ${LibArchive_VERSION}")
else()
    message(STATUS "libarchive not found; archive support will be stubbed until the dependency is installed.")
endif()
