cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GameSproutRuntime VERSION 0.1 LANGUAGES CXX)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include("${CMAKE_CURRENT_LIST_DIR}/../GRHI/GRHI.cmake")

# Automatically include new runtime sources and preserve their on-disk layout.
file(GLOB_RECURSE gamesprout_runtime_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.py"
)
list(FILTER gamesprout_runtime_files EXCLUDE REGEX
    "/(build|Build|cmake-build-[^/]+|CMakeFiles|[^/]+_autogen|Tests|ThirdParty)/")
list(SORT gamesprout_runtime_files)

add_library(GameSproutRuntime SHARED ${gamesprout_runtime_files})
add_library(GameSproutRuntime::GameSproutRuntime ALIAS GameSproutRuntime)

# This entry defines the public API target. The real library alias above keeps
# it from creating an imported binary target inside the implementation project.
include("${CMAKE_CURRENT_SOURCE_DIR}/GameSproutRuntime.cmake")
target_link_libraries(GameSproutRuntime PUBLIC
    GameSproutRuntime::GameSproutRuntimeApi
)
target_link_libraries(GameSproutRuntime PRIVATE GRHI::GRHIApi)
target_include_directories(GameSproutRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(GameSproutRuntime PRIVATE GAMESPROUT_RUNTIME_EXPORTS)

get_filename_component(gamesprout_runtime_output_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Bin" ABSOLUTE)
set(GAMESPROUT_RUNTIME_OUTPUT_DIRECTORY "${gamesprout_runtime_output_directory}" CACHE PATH
    "Runtime binary output directory; isolated verification builds may override this path")
set(gamesprout_runtime_output_directory "${GAMESPROUT_RUNTIME_OUTPUT_DIRECTORY}")
# The generator expression prevents Xcode and other multi-config generators
# from adding Debug/Release directories below Bin.
set(gamesprout_runtime_output "$<1:${gamesprout_runtime_output_directory}>")
set_target_properties(GameSproutRuntime PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    DEBUG_POSTFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${gamesprout_runtime_output}"
    LIBRARY_OUTPUT_DIRECTORY "${gamesprout_runtime_output}"
    ARCHIVE_OUTPUT_DIRECTORY "${gamesprout_runtime_output}"
    PDB_OUTPUT_DIRECTORY "${gamesprout_runtime_output}"
    COMPILE_PDB_OUTPUT_DIRECTORY "${gamesprout_runtime_output}"
    MACOSX_RPATH ON
    XCODE_GENERATE_SCHEME TRUE
)
if(WIN32)
    set_target_properties(GameSproutRuntime PROPERTIES PREFIX "")
elseif(APPLE)
    set_target_properties(GameSproutRuntime PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX)
    set_target_properties(GameSproutRuntime PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${gamesprout_runtime_files})

# Normal builds consume the checked-in cross-platform shader bytes. Regeneration is explicit/offline.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_target(RuntimeGenerateShaders
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders/GenerateShaders.py"
            --shadercross "${CMAKE_CURRENT_SOURCE_DIR}/../../Bin/ShaderTools/bin/shadercross${CMAKE_EXECUTABLE_SUFFIX}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
endif()

option(GAMESPROUT_RUNTIME_BUILD_TESTS "Build Runtime API and coordinate checks" OFF)
if(GAMESPROUT_RUNTIME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# Keep standalone Runtime SDK outputs self-contained for shell and GPU tests.
get_target_property(runtime_backend_dependencies GRHI::GRHI GRHI_RUNTIME_DEPENDENCIES)
add_custom_command(TARGET GameSproutRuntime POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GRHI::GRHI>" ${runtime_backend_dependencies} "$<TARGET_FILE_DIR:GameSproutRuntime>"
    VERBATIM)
