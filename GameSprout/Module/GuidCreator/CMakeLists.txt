cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GuidCreator VERSION 0.1 LANGUAGES CXX)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GRHI/GRHI.cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Keep the target and IDE tree in sync when files are added or removed.
file(GLOB_RECURSE module_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)
# Exclude generated files from standalone builds inside the source tree.
list(FILTER module_files EXCLUDE REGEX "/(build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen)/")
list(APPEND module_files "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

add_library(GuidCreator SHARED ${module_files})
target_compile_features(GuidCreator PRIVATE cxx_std_20)
target_link_libraries(GuidCreator PRIVATE
    Foundation::FoundationApi
    GRHI::GRHIApi
    Qt${QT_VERSION_MAJOR}::Core
)
if(APPLE)
    find_library(guid_core_foundation_framework CoreFoundation REQUIRED)
    target_link_libraries(GuidCreator PRIVATE "${guid_core_foundation_framework}")
elseif(WIN32)
    target_link_libraries(GuidCreator PRIVATE ole32)
endif()
target_include_directories(GuidCreator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../View/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(GuidCreator PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${module_files}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE rhi_adapater_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(GuidCreator PRIVATE ${rhi_adapater_interface_files})
    get_filename_component(module_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    source_group(TREE "${module_root}" FILES ${rhi_adapater_interface_files})
endif()

get_filename_component(gamesprout_bin_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bin" ABSOLUTE
)
if(TARGET GameSproutEditor)
    set(module_output_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Logic/Module")
elseif(APPLE)
    set(module_output_directory
        "${gamesprout_bin_directory}/GameSproutEditor.app/Contents/MacOS/Logic/Module")
else()
    set(module_output_directory "${gamesprout_bin_directory}/Logic/Module")
endif()

set_target_properties(GuidCreator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
)
