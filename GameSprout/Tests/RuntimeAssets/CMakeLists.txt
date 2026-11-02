cmake_minimum_required(VERSION 3.21)
project(RuntimeAssetsChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
add_subdirectory("${gamesprout_root}/Module/RuntimeAssets" RuntimeAssets)
set_target_properties(RuntimeAssets PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules/$<0:>")
add_executable(PhysicsMaterialMissingChecks PhysicsMaterialMissingChecks.cpp)
target_compile_features(PhysicsMaterialMissingChecks PRIVATE cxx_std_20)
target_include_directories(PhysicsMaterialMissingChecks PRIVATE
    "${gamesprout_root}/Module/RuntimeAssets"
    "${gamesprout_root}/Module/Shared"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PhysicsMaterialMissingChecks PRIVATE RuntimeAssets
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set(RUNTIME_ASSETS_TEST_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Runtime directory for isolated provider checks")
enable_testing()
add_test(NAME RuntimeAssets.MissingPhysicsMaterial COMMAND PhysicsMaterialMissingChecks "${RUNTIME_ASSETS_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(RuntimeAssets.MissingPhysicsMaterial PROPERTIES TIMEOUT 30)
