cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ShaderCompiler VERSION 0.1 LANGUAGES CXX)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutRuntime/GameSproutRuntime.cmake")

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
    "${CMAKE_CURRENT_SOURCE_DIR}/*.txt"
)
# Exclude build output, separately built dependencies and test entry points.
list(FILTER module_files EXCLUDE REGEX "/(build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen|ThirdParty|Tests)/")
list(REMOVE_DUPLICATES module_files)

add_library(ShaderCompiler SHARED ${module_files})
target_compile_features(ShaderCompiler PRIVATE cxx_std_20)
target_link_libraries(ShaderCompiler PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi
    Qt${QT_VERSION_MAJOR}::Core
)
target_include_directories(ShaderCompiler PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../View/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(ShaderCompiler PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${module_files}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE rhi_adapater_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(ShaderCompiler PRIVATE ${rhi_adapater_interface_files})
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

set_target_properties(ShaderCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
)

set(GAMESPROUT_SHADER_TOOLS_ROOT "${gamesprout_bin_directory}/ShaderTools" CACHE PATH "Independent SDL shader tools SDK")
target_compile_definitions(ShaderCompiler PRIVATE GAMESPROUT_SHADER_TOOLS_ROOT="${GAMESPROUT_SHADER_TOOLS_ROOT}")
if(EXISTS "${GAMESPROUT_SHADER_TOOLS_ROOT}/toolchain.json")
    add_custom_target(ShaderCompilerDeployTools
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:ShaderCompiler>/../../local_tools/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${GAMESPROUT_SHADER_TOOLS_ROOT}" "$<TARGET_FILE_DIR:ShaderCompiler>/../../local_tools/shaders"
        COMMENT "Deploying SDL shader compiler tools" VERBATIM)
    add_dependencies(ShaderCompiler ShaderCompilerDeployTools)
else()
    message(STATUS "Shader tools missing: build/install GameSprout/Tools/ShaderTools")
endif()
