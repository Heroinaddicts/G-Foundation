cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ScriptCompiler VERSION 0.1 LANGUAGES CXX)
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
)
# Exclude build output, separately built dependencies and test entry points.
list(FILTER module_files EXCLUDE REGEX "/(build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen|ThirdParty|Tests|Tools)/")
list(APPEND module_files "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

add_library(ScriptCompiler SHARED ${module_files})
target_compile_features(ScriptCompiler PRIVATE cxx_std_20)
target_link_libraries(ScriptCompiler PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi
    Qt${QT_VERSION_MAJOR}::Core
)
target_include_directories(ScriptCompiler PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../View/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(ScriptCompiler PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${module_files}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE rhi_adapater_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(ScriptCompiler PRIVATE ${rhi_adapater_interface_files})
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

set_target_properties(ScriptCompiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
)

get_filename_component(script_sdk_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutRuntime/Api" ABSOLUTE)
set(script_build_configuration "${CMAKE_BUILD_TYPE}")
if(NOT script_build_configuration)
    set(script_build_configuration "Debug")
endif()
target_include_directories(ScriptCompiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Reflection must use the same kit flags as the native compilation, including
# layout-affecting defines. Tokenize once with CMake's native command parser.
function(ScriptCompilerEscapeCString input output)
    string(REPLACE "\\" "\\\\" escaped "${input}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    string(REPLACE "\r" "\\r" escaped "${escaped}")
    set(${output} "${escaped}" PARENT_SCOPE)
endfunction()
string(TOUPPER "${script_build_configuration}" script_configuration_upper)
set(script_native_configuration_flags "${CMAKE_CXX_FLAGS_${script_configuration_upper}}")
ScriptCompilerEscapeCString("${CMAKE_CXX_FLAGS}" script_base_flags_escaped)
ScriptCompilerEscapeCString("${script_native_configuration_flags}" script_configuration_flags_escaped)
separate_arguments(script_ast_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS} ${script_native_configuration_flags}")
set(script_ast_flag_literals "")
foreach(script_flag IN LISTS script_ast_flags)
    ScriptCompilerEscapeCString("${script_flag}" script_flag_escaped)
    list(APPEND script_ast_flag_literals "\"${script_flag_escaped}\"")
endforeach()
list(JOIN script_ast_flag_literals "," script_ast_flags_cpp)
configure_file(ScriptToolchain.h.in "${CMAKE_CURRENT_BINARY_DIR}/ScriptToolchain.h" @ONLY)

# The external IDE invokes the same compiler module as Editor. This executable
# belongs to ScriptCompiler and does not bootstrap an Editor, Runtime or View.
add_executable(GameSproutScriptBuild Tools/ScriptCompilerCommand.cpp)
target_compile_features(GameSproutScriptBuild PRIVATE cxx_std_20)
target_include_directories(GameSproutScriptBuild PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api")
target_link_libraries(GameSproutScriptBuild PRIVATE ScriptCompiler
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt${QT_VERSION_MAJOR}::Core)
if(TARGET GameSproutEditor)
    set(script_command_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Tools")
    add_dependencies(GameSproutEditor GameSproutScriptBuild)
else()
    # Derive from the final module output property so isolated test projects can
    # redirect ScriptCompiler without this tool writing into the user's Bin.
    set(script_command_directory "$<TARGET_FILE_DIR:ScriptCompiler>/../../Tools")
endif()
set_target_properties(GameSproutScriptBuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${script_command_directory}/$<0:>")
if(APPLE)
    set_property(TARGET GameSproutScriptBuild APPEND PROPERTY BUILD_RPATH "@loader_path/../Logic/Module")
elseif(UNIX)
    set_property(TARGET GameSproutScriptBuild APPEND PROPERTY BUILD_RPATH "$ORIGIN/../Logic/Module")
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/ScriptCompilerCommand.cpp")
