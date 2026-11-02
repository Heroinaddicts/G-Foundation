cmake_minimum_required(VERSION 3.21)
project(CodeEditorBackendChecks LANGUAGES CXX)
if(NOT APPLE OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Xcode backend checks require macOS; the public CodeEditor module still provides an unsupported-platform backend")
    return()
endif()
find_package(Qt6 REQUIRED COMPONENTS Core)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(CodeEditorBackendChecks CodeEditorBackendChecks.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../CodeEditorBackend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../CodeEditorXcodeBackend.cpp")
target_compile_features(CodeEditorBackendChecks PRIVATE cxx_std_20)
target_include_directories(CodeEditorBackendChecks PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_include_directories(CodeEditorBackendChecks PRIVATE "${gamesprout_root}/GameSproutRuntime/Api")
target_link_libraries(CodeEditorBackendChecks PRIVATE Qt6::Core)
target_compile_definitions(CodeEditorBackendChecks PRIVATE
    TEST_CMAKE_PROGRAM="${CMAKE_COMMAND}"
    TEST_COMPILER_PROGRAM="${CMAKE_CXX_COMPILER}"
    TEST_RUNTIME_SDK="${gamesprout_root}/GameSproutRuntime/Api")
enable_testing()
set(CODE_EDITOR_TEST_BUILD_TOOL "" CACHE FILEPATH "Optional independently built GameSproutScriptBuild for the real Xcode/pipeline integration check")
if(APPLE)
    add_test(NAME CodeEditor.XcodeProject COMMAND CodeEditorBackendChecks "${CODE_EDITOR_TEST_BUILD_TOOL}")
    set_tests_properties(CodeEditor.XcodeProject PROPERTIES TIMEOUT 180)
endif()
