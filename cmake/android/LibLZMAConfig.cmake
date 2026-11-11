# libarchive uses the legacy LibLZMA variables. Bridge them to our source-built
# target without searching the host filesystem during an Android cross-build.
if(NOT ANDROID OR NOT TARGET liblzma)
    message(FATAL_ERROR "The Android liblzma target must be configured before libarchive")
endif()
set(LIBLZMA_FOUND TRUE)
set(LibLZMA_FOUND TRUE)
set(LIBLZMA_INCLUDE_DIR "${labelqt_xz_SOURCE_DIR}/src/liblzma/api")
set(LIBLZMA_INCLUDE_DIRS "${LIBLZMA_INCLUDE_DIR}")
set(LIBLZMA_LIBRARIES liblzma)
# The pinned 5.8.4 build includes this API. It cannot be link-probed until the
# FetchContent target is built; the final Android link verifies the dependency.
set(HAVE_LZMA_STREAM_ENCODER_MT TRUE CACHE INTERNAL "Bundled liblzma API")
