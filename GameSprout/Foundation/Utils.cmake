include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/UtilsApi.cmake")

# SDK users link the independently built archive, never its implementation.
if(NOT TARGET Foundation::Utils)
    add_library(Foundation::Utils STATIC IMPORTED GLOBAL)
    get_filename_component(foundation_utils_bin "${CMAKE_CURRENT_LIST_DIR}/../../Bin" ABSOLUTE)
    set_target_properties(Foundation::Utils PROPERTIES
        IMPORTED_LOCATION "${foundation_utils_bin}/${CMAKE_STATIC_LIBRARY_PREFIX}FoundationUtils${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_LINK_LIBRARIES Foundation::UtilsApi
    )
    if(WIN32)
        set_property(TARGET Foundation::Utils APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES dbghelp psapi shlwapi)
    elseif(APPLE OR UNIX)
        find_package(Threads REQUIRED)
        find_package(Iconv REQUIRED)
        set_property(TARGET Foundation::Utils APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES Threads::Threads Iconv::Iconv)
    else()
        message(FATAL_ERROR "Foundation Utils does not support the current platform.")
    endif()
endif()

# Preserve the existing directory-level Utils dependency for consumers.
link_libraries(Foundation::Utils)
