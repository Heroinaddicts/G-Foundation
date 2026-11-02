cmake_minimum_required(VERSION 3.21)
project(CodeEditorCoordinatorChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
add_subdirectory("${gamesprout_root}/Module/CodeEditor" CodeEditor)
set_target_properties(CodeEditor PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_executable(CodeEditorCoordinatorChecks CodeEditorCoordinatorChecks.cpp)
target_compile_features(CodeEditorCoordinatorChecks PRIVATE cxx_std_20)
target_include_directories(CodeEditorCoordinatorChecks PRIVATE
    "${gamesprout_root}/Module/CodeEditor"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(CodeEditorCoordinatorChecks PRIVATE CodeEditor
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core ${CMAKE_DL_LIBS})
# Only the fixture injects a worker backend and inspects owner/queue boundaries.
# Production modules keep their ordinary private implementation boundaries.
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
    target_compile_options(CodeEditorCoordinatorChecks PRIVATE -fno-access-control)
else()
    message(FATAL_ERROR "CodeEditorCoordinatorChecks currently requires Clang or GCC")
endif()
set_target_properties(CodeEditorCoordinatorChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
set(CODE_EDITOR_TEST_FOUNDATION_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Independently built Foundation directory")
enable_testing()
add_test(NAME CodeEditor.Coordinator COMMAND CodeEditorCoordinatorChecks "${CODE_EDITOR_TEST_FOUNDATION_DIRECTORY}")
set_tests_properties(CodeEditor.Coordinator PROPERTIES TIMEOUT 30)
