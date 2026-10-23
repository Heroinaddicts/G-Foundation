cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(DockerLayoutManager VERSION 0.1 LANGUAGES CXX)
endif()

# Foundation's entry point propagates its API and Utils include paths.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../GRHI/GRHI.cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(DockerLayoutManager SHARED
    Header.h
    ExportView.cpp
    DockerLayoutManager.cpp
    DockerLayoutManager.h
    DockerLayoutManager.ui
    CMakeLists.txt
)
target_compile_features(DockerLayoutManager PRIVATE cxx_std_20)
target_link_libraries(DockerLayoutManager PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Foundation::FoundationApi
    GRHI::GRHIApi
)
target_include_directories(DockerLayoutManager PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(DockerLayoutManager PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Header.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExportView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DockerLayoutManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DockerLayoutManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/DockerLayoutManager.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)

# Keep shared interfaces visible when opening DockerLayoutManager by itself as well.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE view_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(DockerLayoutManager PRIVATE ${view_interface_files})
    get_filename_component(view_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    source_group(TREE "${view_root}" FILES ${view_interface_files})
endif()

# Use the launcher target when included in a combined build. Standalone
# builds follow the same layout without having to configure the launcher.
get_filename_component(gamesprout_bin_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bin" ABSOLUTE
)
if(TARGET GameSprout)
    set(view_output_directory "$<TARGET_FILE_DIR:GameSprout>/Logic/View")
elseif(APPLE)
    set(view_output_directory
        "${gamesprout_bin_directory}/GameSprout.app/Contents/MacOS/Logic/View")
else()
    set(view_output_directory "${gamesprout_bin_directory}/Logic/View")
endif()

# The generator expression suppresses automatic configuration subdirectories.
set_target_properties(DockerLayoutManager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
)
