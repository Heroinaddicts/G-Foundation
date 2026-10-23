cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GameSproutModules LANGUAGES CXX)
endif()

# Shared interface headers are project files, not a compiled logic module.
file(GLOB_RECURSE module_interface_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Interface/*"
)
add_custom_target(GameSproutModuleInterfaces SOURCES ${module_interface_files})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${module_interface_files}
)

# Each module owns its target and can also be opened as a standalone project.
file(GLOB module_directories CONFIGURE_DEPENDS
    LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*"
)
list(SORT module_directories)
foreach(module_directory IN LISTS module_directories)
    if(IS_DIRECTORY "${module_directory}" AND EXISTS "${module_directory}/CMakeLists.txt")
        add_subdirectory("${module_directory}")
    endif()
endforeach()
