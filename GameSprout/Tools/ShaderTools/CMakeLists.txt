cmake_minimum_required(VERSION 3.24)
project(GameSproutShaderTools LANGUAGES C CXX)

# This is an independent host-tools build, never an editor/runtime subdirectory.
include(FetchContent)
get_filename_component(repository_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${repository_root}/Bin/ShaderTools" CACHE PATH "Host shader tools SDK" FORCE)
endif()
find_package(SDL3 3.2 CONFIG REQUIRED HINTS "${repository_root}/../SDL/build")

include("${CMAKE_CURRENT_LIST_DIR}/DXCPackage.cmake")

set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(spirv_cross
    URL "https://codeload.github.com/KhronosGroup/SPIRV-Cross/tar.gz/1a6169566c73d3da552748fc372fe2bbb856e46e"
    URL_HASH SHA256=0f295b214b164e42a1d21537c8da7b44569806c16220dda9798558edfaacd11e)
FetchContent_MakeAvailable(spirv_cross)

FetchContent_Declare(shadercross
    URL "https://codeload.github.com/libsdl-org/SDL_shadercross/tar.gz/1ff05bec573988a98ef9e0260b4da44f512b8367"
    URL_HASH SHA256=60aae35ff71bd1d5f85b363e9d077a86bb27dd36333c42685e56c2fd541174d2
    SOURCE_SUBDIR host-tools-source-only)
FetchContent_MakeAvailable(shadercross)
add_library(GameSproutShaderCross STATIC "${shadercross_SOURCE_DIR}/src/SDL_shadercross.c")
target_include_directories(GameSproutShaderCross PUBLIC "${shadercross_SOURCE_DIR}/include")
target_link_libraries(GameSproutShaderCross PUBLIC SDL3::SDL3-shared PRIVATE spirv-cross-c GameSproutDXC)
target_compile_definitions(GameSproutShaderCross PRIVATE SDL_SHADERCROSS_DXC)
set_target_properties(GameSproutShaderCross PROPERTIES LINKER_LANGUAGE CXX)
add_executable(shadercross "${shadercross_SOURCE_DIR}/src/cli.c")
target_link_libraries(shadercross PRIVATE GameSproutShaderCross)
set_target_properties(shadercross PROPERTIES LINKER_LANGUAGE CXX)

# Official SDL_shadercross stack: DXC + SPIRV-Cross.
if(APPLE)
    set(shader_tools_origin "@loader_path")
elseif(UNIX)
    set(shader_tools_origin "$ORIGIN")
endif()
set_target_properties(shadercross PROPERTIES INSTALL_RPATH "${shader_tools_origin}/../lib")
install(TARGETS shadercross spirv-cross RUNTIME DESTINATION bin)
install(IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3-shared RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
install(FILES "${shadercross_SOURCE_DIR}/LICENSE.txt" DESTINATION Licenses/SDL_shadercross)
install(FILES "${spirv_cross_SOURCE_DIR}/LICENSE" DESTINATION Licenses/SPIRV-Cross)
install(FILES "${repository_root}/../SDL/LICENSE.txt" DESTINATION Licenses/SDL3 OPTIONAL)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/toolchain.json"
    "{\"Toolchain\":\"SDLShaderCompiler\",\"DXC\":\"${dxc_identity}\",\"DXCLibrary\":\"${dxc_library_identity}\",\"ShaderCross\":\"1ff05bec573988a98ef9e0260b4da44f512b8367\",\"SPIRVCross\":\"1a6169566c73d3da552748fc372fe2bbb856e46e\",\"Host\":\"${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}\"}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/toolchain.json" DESTINATION .)
