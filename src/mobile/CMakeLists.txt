find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Svg)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0002 NEW)

if(ANDROID)
    enable_language(C)
    include(FetchContent)
    # Android ships zlib; the transfer format only needs ZIP store/deflate.
    foreach(feature IN ITEMS WERROR OPENSSL MD LIBB2 LZ4 LZO LZMA ZSTD BZip2 LIBXML2
            BSDXML EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX CNG TAR CPIO CAT UNZIP
            XATTR ACL ICONV TEST INSTALL)
        set(ENABLE_${feature} OFF CACHE BOOL "libarchive mobile configuration" FORCE)
    endforeach()
    set(ENABLE_ZLIB ON CACHE BOOL "ZIP compression" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(labelqt_archive
        URL https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.xz
        URL_HASH SHA256=a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(labelqt_archive)
    set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(LABELQT_ARCHIVE_TARGET archive_static)
else()
    find_package(LibArchive 3.6 REQUIRED)
    set(LABELQT_ARCHIVE_TARGET LibArchive::LibArchive)
endif()

add_library(LabelQtTransfer STATIC
    ../services/transfer/ProjectTransferService.cpp
    ../services/transfer/TransferSupport.cpp
    ../services/transfer/WorkspaceStorageService.cpp
)
target_link_libraries(LabelQtTransfer PUBLIC LabelQtShared PRIVATE ${LABELQT_ARCHIVE_TARGET})
if(ANDROID)
    target_include_directories(LabelQtTransfer PRIVATE "${labelqt_archive_SOURCE_DIR}/libarchive")
endif()
set_target_properties(LabelQtTransfer PROPERTIES POSITION_INDEPENDENT_CODE ON)
labelqt_enable_warnings(LabelQtTransfer)

add_library(LabelQtMobilePresentation STATIC
    MobileDocument.cpp MobileDocument.h
    MobileLabelModel.cpp MobileLabelModel.h
    MobileWorkspaceController.cpp MobileWorkspaceController.h
    MobileWorkspaceModel.cpp MobileWorkspaceModel.h
    MobileReviewController.cpp MobileReviewController.h
)
target_link_libraries(LabelQtMobilePresentation PUBLIC LabelQtTransfer Qt6::Concurrent)
set_target_properties(LabelQtMobilePresentation PROPERTIES POSITION_INDEPENDENT_CODE ON)
labelqt_enable_warnings(LabelQtMobilePresentation)

qt_add_executable(LabelQtMobile
    main.cpp
    ../platform/android/AndroidFileBridge.cpp
    ../platform/android/AndroidFileBridge.h
)
target_link_libraries(LabelQtMobile PRIVATE LabelQtMobilePresentation Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
labelqt_enable_warnings(LabelQtMobile)
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/ProjectCanvas.qml PROPERTIES QT_RESOURCE_ALIAS ProjectCanvas.qml)
set_source_files_properties(qml/IconButton.qml PROPERTIES QT_RESOURCE_ALIAS IconButton.qml)
set_source_files_properties(qml/SubmitDialog.qml PROPERTIES QT_RESOURCE_ALIAS SubmitDialog.qml)
set_source_files_properties(qml/WorkspaceBrowser.qml PROPERTIES QT_RESOURCE_ALIAS WorkspaceBrowser.qml)
set(LABELQT_MOBILE_ICONS menu undo-2 redo-2 ellipsis chevron-left chevron-right maximize x keyboard trash-2)
set(LABELQT_MOBILE_ICON_FILES)
foreach(icon IN LISTS LABELQT_MOBILE_ICONS)
    set(path "qml/icons/${icon}.svg")
    set_source_files_properties(${path} PROPERTIES QT_RESOURCE_ALIAS "icons/${icon}.svg")
    list(APPEND LABELQT_MOBILE_ICON_FILES "${path}")
endforeach()
set_source_files_properties(qml/icons/LICENSE PROPERTIES QT_RESOURCE_ALIAS "icons/LICENSE")
qt_add_qml_module(LabelQtMobile
    URI LabelQt.Mobile VERSION 1.0
    QML_FILES qml/Main.qml qml/ProjectCanvas.qml qml/IconButton.qml qml/SubmitDialog.qml qml/WorkspaceBrowser.qml
    RESOURCES ${LABELQT_MOBILE_ICON_FILES} qml/icons/LICENSE
)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(LabelQtMobile SOURCES ${LABELQT_TRANSLATION_SOURCES}
        TS_FILES ${LABELQT_TRANSLATION_FILES} RESOURCE_PREFIX /i18n)
endif()
if(ANDROID)
    set_target_properties(LabelQtMobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME org.labelqt.mobile
        QT_ANDROID_VERSION_NAME "0.1.0-android-preview.1"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
    )
endif()
