include_guard(GLOBAL)

set(FOUNDATION_UTILS_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/Utils")

file(GLOB_RECURSE FOUNDATION_UTILS_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${FOUNDATION_UTILS_SOURCE_DIR}/*.hpp"
)
list(SORT FOUNDATION_UTILS_FILES)

if(NOT TARGET FoundationUtilsApi)
    add_library(FoundationUtilsApi INTERFACE)
    add_library(Foundation::UtilsApi ALIAS FoundationUtilsApi)
    set_target_properties(FoundationUtilsApi PROPERTIES
        EXPORT_NAME UtilsApi
    )
    target_compile_features(FoundationUtilsApi INTERFACE cxx_std_20)
    target_include_directories(FoundationUtilsApi INTERFACE
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:SDK/Include/Foundation/Utils>"
    )
    target_compile_options(FoundationUtilsApi INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
        "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>"
    )
    target_compile_definitions(FoundationUtilsApi INTERFACE
        "$<$<PLATFORM_ID:Windows>:WIN32>"
        "$<$<PLATFORM_ID:Windows>:NOMINMAX>"
        "$<$<PLATFORM_ID:Darwin>:FOUNDATION_MACOS>"
        "$<$<PLATFORM_ID:Linux>:Linux>"
    )
endif()

