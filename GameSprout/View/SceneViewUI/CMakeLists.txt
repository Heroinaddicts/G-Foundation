cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SceneViewUI VERSION 0.1 LANGUAGES CXX)
endif()

# Foundation's entry point propagates its API and Utils include paths.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GRHI/GRHI.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutRuntime/GameSproutRuntime.cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# Keep the target and IDE tree in sync when files are added or removed.
file(GLOB_RECURSE view_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qss"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*"
)
# Exclude generated files from standalone builds inside the source tree.
list(FILTER view_files EXCLUDE REGEX "/(build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen)/")
list(APPEND view_files "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

add_library(SceneViewUI SHARED ${view_files})
target_compile_features(SceneViewUI PRIVATE cxx_std_20)
target_link_libraries(SceneViewUI PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Foundation::FoundationApi
    GRHI::GRHIApi
    GameSproutRuntime::GameSproutRuntimeApi
)
target_include_directories(SceneViewUI PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Module/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
# Let AUTOUIC also find forms stored in subdirectories.
set(view_ui_directories)
foreach(view_file IN LISTS view_files)
    if(view_file MATCHES "[.]ui$")
        get_filename_component(view_ui_directory "${view_file}" DIRECTORY)
        list(APPEND view_ui_directories "${view_ui_directory}")
    endif()
endforeach()
list(REMOVE_DUPLICATES view_ui_directories)

set_target_properties(SceneViewUI PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${view_ui_directories}"
    AUTORCC ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${view_files}
)

# Keep shared interfaces visible when opening SceneViewUI by itself as well.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE view_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(SceneViewUI PRIVATE ${view_interface_files})
    get_filename_component(view_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    source_group(TREE "${view_root}" FILES ${view_interface_files})
endif()

# Use the launcher target when included in a combined build. Standalone
# builds follow the same layout without having to configure the launcher.
get_filename_component(gamesprout_bin_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bin" ABSOLUTE
)
if(TARGET GameSproutEditor)
    set(view_output_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Logic/View")
elseif(APPLE)
    set(view_output_directory
        "${gamesprout_bin_directory}/GameSproutEditor.app/Contents/MacOS/Logic/View")
else()
    set(view_output_directory "${gamesprout_bin_directory}/Logic/View")
endif()

# The generator expression suppresses automatic configuration subdirectories.
set_target_properties(SceneViewUI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${view_output_directory}/$<0:>"
)
