cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GameSproutViews LANGUAGES CXX)
endif()

# Shared interface headers are project files, not a compiled view module.
file(GLOB_RECURSE view_interface_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Interface/*"
)
add_custom_target(GameSproutViewInterfaces SOURCES ${view_interface_files})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${view_interface_files}
)

# Each view owns its target and can also be opened as a standalone project.
file(GLOB view_directories CONFIGURE_DEPENDS
    LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*"
)
list(SORT view_directories)
foreach(view_directory IN LISTS view_directories)
    if(IS_DIRECTORY "${view_directory}" AND EXISTS "${view_directory}/CMakeLists.txt")
        add_subdirectory("${view_directory}")
    endif()
endforeach()
