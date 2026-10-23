cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../Foundation/ParallelBuild.cmake")

# Make the real compiler flags and include paths available to code editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GRHI VERSION 0.1 LANGUAGES CXX)
endif()

# SDL is built independently; only its prebuilt shared target is consumed here.
find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3-shared
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../SDL/build"
)
include(CMakePushCheckState)
include(CheckCXXSourceCompiles)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES SDL3::SDL3-shared)
unset(GRHI_SDL_ADAPTER_EXTENSION CACHE)
check_cxx_source_compiles("#include <SDL3/SDL.h>
#if !defined(SDL_GRHI_GPU_ADAPTERS_VERSION) || SDL_GRHI_GPU_ADAPTERS_VERSION < 2
#error GRHI requires the local SDL GPU adapter extension
#endif
int main() { int count = 0; SDL_free(SDL_GetGPUAdapters(&count)); return SDL_GetGPUAdapterExtensionVersion() < 2; }"
    GRHI_SDL_ADAPTER_EXTENSION
)
cmake_pop_check_state()
if(NOT GRHI_SDL_ADAPTER_EXTENSION)
    message(FATAL_ERROR "Build the local SDL with the GRHI GPU adapter extension first, then set SDL3_DIR to that build.")
endif()

file(GLOB_RECURSE grhi_api_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/GRHIApi/*"
)
file(GLOB_RECURSE grhi_implementation_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/GRHI/*"
)
set(grhi_project_files
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/GRHI.cmake"
)
set(grhi_files
    ${grhi_api_files}
    ${grhi_implementation_files}
    ${grhi_project_files}
)
list(SORT grhi_files)

# Xcode normally omits empty source groups. Folder references preserve empty
# backend directories without adding placeholder files to the source tree.
if(CMAKE_GENERATOR STREQUAL "Xcode")
    file(GLOB_RECURSE grhi_entries CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${CMAKE_CURRENT_SOURCE_DIR}/GRHI/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/GRHIApi/*"
    )
    foreach(grhi_entry IN LISTS grhi_entries)
        if(IS_DIRECTORY "${grhi_entry}")
            file(GLOB grhi_children CONFIGURE_DEPENDS "${grhi_entry}/*")
            if(NOT grhi_children)
                list(APPEND grhi_files "${grhi_entry}")
                list(APPEND grhi_empty_directories "${grhi_entry}")
                set_source_files_properties("${grhi_entry}" PROPERTIES
                    HEADER_FILE_ONLY TRUE
                    XCODE_EXPLICIT_FILE_TYPE folder
                )
            endif()
        endif()
    endforeach()
endif()

add_library(GRHI SHARED ${grhi_files})
add_library(GRHI::GRHI ALIAS GRHI)

include("${CMAKE_CURRENT_SOURCE_DIR}/GRHI.cmake")
target_link_libraries(GRHI PUBLIC GRHI::GRHIApi PRIVATE SDL3::SDL3-shared)

target_compile_features(GRHI PUBLIC cxx_std_20)
# Matches GRHIExport.h; consumers must not inherit the export definition.
target_compile_definitions(GRHI PRIVATE EXPORTS)
option(GRHI_GPU_DEBUG "Enable SDL GPU validation (requires a compatible SDL build)" OFF)
if(GRHI_GPU_DEBUG)
    target_compile_definitions(GRHI PRIVATE GRHI_GPU_DEBUG)
endif()
# Use Bin directly, including with multi-configuration generators such as Xcode.
set(grhi_output_directory "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../Bin>")
set_target_properties(GRHI PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${grhi_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${grhi_output_directory}"
    RUNTIME_OUTPUT_DIRECTORY "${grhi_output_directory}"
    MACOSX_RPATH ON
    XCODE_GENERATE_SCHEME TRUE
)
if(WIN32)
    # Match the public loader's GRHI.dll name with MinGW as well as MSVC.
    set_target_properties(GRHI PROPERTIES PREFIX "")
endif()

target_include_directories(GRHI
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/GRHI"
)

# Deploy SDL beside GRHI. Consumers must deploy the complete runtime directory.
if(APPLE)
    set_target_properties(GRHI PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX)
    set_target_properties(GRHI PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()
if(UNIX)
    set(grhi_sdl_runtime_name "$<TARGET_SONAME_FILE_NAME:SDL3::SDL3-shared>")
else()
    set(grhi_sdl_runtime_name "$<TARGET_FILE_NAME:SDL3::SDL3-shared>")
endif()
add_custom_command(TARGET GRHI POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:SDL3::SDL3-shared>"
        "$<TARGET_FILE_DIR:GRHI>/${grhi_sdl_runtime_name}"
    VERBATIM
)

# All paths are relative to the GRHI project root, never grouped by extension.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${grhi_files})

foreach(grhi_directory IN LISTS grhi_empty_directories)
    file(RELATIVE_PATH grhi_relative_directory
        "${CMAKE_CURRENT_SOURCE_DIR}" "${grhi_directory}"
    )
    get_filename_component(grhi_parent_group "${grhi_relative_directory}" DIRECTORY)
    source_group("${grhi_parent_group}" FILES "${grhi_directory}")
endforeach()
