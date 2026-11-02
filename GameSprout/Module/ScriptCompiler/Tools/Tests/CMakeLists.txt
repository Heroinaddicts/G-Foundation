cmake_minimum_required(VERSION 3.21)
project(ScriptCompilerCommandChecks LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_subdirectory("${gamesprout_root}/Module/ScriptCompiler" ScriptCompiler)
set_target_properties(ScriptCompiler PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
set_target_properties(GameSproutScriptBuild PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/Tools/$<0:>")
add_executable(ScriptCompilerCommandChecks ScriptCompilerCommandChecks.cpp)
target_compile_features(ScriptCompilerCommandChecks PRIVATE cxx_std_20)
target_include_directories(ScriptCompilerCommandChecks PRIVATE
    "${gamesprout_root}/Module/ScriptCompiler"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ScriptCompilerCommandChecks PRIVATE ScriptCompiler
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set_target_properties(ScriptCompilerCommandChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
enable_testing()
add_test(NAME ScriptCompiler.CommandLine COMMAND ScriptCompilerCommandChecks "$<TARGET_FILE:GameSproutScriptBuild>")
set_tests_properties(ScriptCompiler.CommandLine PROPERTIES TIMEOUT 150)
