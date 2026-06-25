if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "DEST_DIR is required")
endif()

file(REMOVE_RECURSE "${DEST_DIR}")
file(MAKE_DIRECTORY "${DEST_DIR}")

file(GLOB_RECURSE LABELQT_SCRIPT_ENTRIES
    RELATIVE "${SOURCE_DIR}"
    LIST_DIRECTORIES true
    "${SOURCE_DIR}/*"
)

foreach(entry IN LISTS LABELQT_SCRIPT_ENTRIES)
    if(entry MATCHES "(^|/)__pycache__(/|$)"
        OR entry MATCHES "\\.py[co]$"
        OR entry MATCHES "(^|/)config\\.json$")
        continue()
    endif()

    set(source_path "${SOURCE_DIR}/${entry}")
    set(dest_path "${DEST_DIR}/${entry}")
    if(IS_DIRECTORY "${source_path}")
        file(MAKE_DIRECTORY "${dest_path}")
    else()
        get_filename_component(dest_parent "${dest_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_parent}")
        file(COPY_FILE "${source_path}" "${dest_path}" ONLY_IF_DIFFERENT)
    endif()
endforeach()
