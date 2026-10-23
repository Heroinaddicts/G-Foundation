include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/Utils.cmake")

set(API_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/FoundationApi"
)

file(GLOB_RECURSE API_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${API_SOURCE_DIR}/*"
)
list(SORT API_FILES)

set(API_IMPLEMENTATION_SOURCES
    ${API_FILES}
)
list(FILTER API_IMPLEMENTATION_SOURCES
    INCLUDE REGEX "\\.(c|cc|cpp|cxx)$"
)

if(NOT TARGET FoundationApi)
    add_library(FoundationApi INTERFACE)

    target_compile_features(FoundationApi INTERFACE cxx_std_20)

    target_include_directories(FoundationApi INTERFACE
        "$<BUILD_INTERFACE:${API_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:SDK/Include/Foundation/Foundation>"
    )
    target_link_libraries(FoundationApi INTERFACE Foundation::UtilsApi)
    set_target_properties(FoundationApi PROPERTIES EXPORT_NAME FoundationApi)
endif()

if(NOT TARGET Foundation::FoundationApi)
    add_library(Foundation::FoundationApi ALIAS FoundationApi)
endif()
