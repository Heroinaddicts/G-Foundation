include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/FoundationApi.cmake")

# Runtime deployment metadata only: this does not build Foundation or resolve
# its private third-party dependencies. Consumers load it through the API.
if(NOT TARGET Foundation::Foundation)
    add_library(Foundation::Foundation SHARED IMPORTED GLOBAL)
    get_filename_component(foundation_sdk_bin "${CMAKE_CURRENT_LIST_DIR}/../Bin" ABSOLUTE)
    set(foundation_runtime_name "${CMAKE_SHARED_LIBRARY_PREFIX}Foundation${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set_target_properties(Foundation::Foundation PROPERTIES
        IMPORTED_LOCATION "${foundation_sdk_bin}/${foundation_runtime_name}"
        INTERFACE_LINK_LIBRARIES Foundation::FoundationApi
    )
    if(WIN32)
        set_property(TARGET Foundation::Foundation PROPERTY IMPORTED_IMPLIB
            "${foundation_sdk_bin}/${CMAKE_IMPORT_LIBRARY_PREFIX}Foundation${CMAKE_IMPORT_LIBRARY_SUFFIX}")
    endif()
endif()
