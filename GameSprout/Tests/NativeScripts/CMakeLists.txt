cmake_minimum_required(VERSION 3.21)
project(NativeScriptChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
add_subdirectory("${gamesprout_root}/Module/ScriptCompiler" ScriptCompiler)
add_subdirectory("${gamesprout_root}/Module/ScriptEditor" ScriptEditor)
add_subdirectory("${gamesprout_root}/Module/ObjectEditing" ObjectEditing)
# Test binaries are isolated from the Editor app and repository Bin.
foreach(target IN ITEMS ScriptCompiler ScriptEditor ObjectEditing)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
endforeach()
set_target_properties(GameSproutScriptBuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/Tools/$<0:>")
add_executable(NativeScriptChecks NativeScriptChecks.cpp)
target_compile_features(NativeScriptChecks PRIVATE cxx_std_20)
target_include_directories(NativeScriptChecks PRIVATE
    "${gamesprout_root}/Module/ScriptCompiler"
    "${gamesprout_root}/Module/ScriptEditor"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(NativeScriptChecks PRIVATE ScriptCompiler ScriptEditor
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
# This integration fixture reaches the candidate-loader transaction directly;
# production modules retain their normal C++ access checks and public boundaries.
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
    target_compile_options(NativeScriptChecks PRIVATE -fno-access-control)
else()
    message(FATAL_ERROR "NativeScriptChecks currently requires Clang or GCC for private integration-fixture access")
endif()
target_compile_definitions(NativeScriptChecks PRIVATE TEST_NATIVE_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(NativeScriptChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
set(SCRIPT_TEST_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Directory containing independently built Runtime matching this SDK")
enable_testing()
add_test(NAME NativeScripts.CompilerAndReload COMMAND NativeScriptChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(NativeScripts.CompilerAndReload PROPERTIES TIMEOUT 180)
add_executable(ScriptFieldSerializationChecks ScriptFieldSerializationChecks.cpp)
target_compile_features(ScriptFieldSerializationChecks PRIVATE cxx_std_20)
target_include_directories(ScriptFieldSerializationChecks PRIVATE
    "${gamesprout_root}/Module/ScriptCompiler"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ScriptFieldSerializationChecks PRIVATE ScriptCompiler Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set_target_properties(ScriptFieldSerializationChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_test(NAME NativeScripts.PrivateProtectedReferences COMMAND ScriptFieldSerializationChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(NativeScripts.PrivateProtectedReferences PROPERTIES TIMEOUT 120)
add_executable(ScriptFieldModuleChecks ScriptFieldModuleChecks.cpp)
target_compile_features(ScriptFieldModuleChecks PRIVATE cxx_std_20)
target_include_directories(ScriptFieldModuleChecks PRIVATE
    "${gamesprout_root}/Module/ScriptEditor"
    "${gamesprout_root}/Module/ObjectEditing"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ScriptFieldModuleChecks PRIVATE ScriptEditor ObjectEditing Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
target_compile_options(ScriptFieldModuleChecks PRIVATE -fno-access-control)
set_target_properties(ScriptFieldModuleChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_test(NAME NativeScripts.FieldService COMMAND ScriptFieldModuleChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
add_executable(ScriptDeclarationChecks ScriptDeclarationChecks.cpp)
target_compile_features(ScriptDeclarationChecks PRIVATE cxx_std_20)
target_include_directories(ScriptDeclarationChecks PRIVATE
    "${gamesprout_root}/Module/ScriptCompiler"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ScriptDeclarationChecks PRIVATE ScriptCompiler
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set_target_properties(ScriptDeclarationChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_test(NAME NativeScripts.DeclarationDiagnostics COMMAND ScriptDeclarationChecks)
set_tests_properties(NativeScripts.DeclarationDiagnostics PROPERTIES TIMEOUT 120)
add_executable(ScriptBuildDependenciesChecks ScriptBuildDependenciesChecks.cpp)
target_compile_features(ScriptBuildDependenciesChecks PRIVATE cxx_std_20)
target_include_directories(ScriptBuildDependenciesChecks PRIVATE "${gamesprout_root}/Module/ScriptCompiler")
target_link_libraries(ScriptBuildDependenciesChecks PRIVATE ScriptCompiler Qt6::Core)
set_target_properties(ScriptBuildDependenciesChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_test(NAME NativeScripts.ContentDependencies COMMAND ScriptBuildDependenciesChecks)
add_executable(ScriptRecursiveFieldsChecks ScriptRecursiveFieldsChecks.cpp)
target_compile_features(ScriptRecursiveFieldsChecks PRIVATE cxx_std_20)
target_include_directories(ScriptRecursiveFieldsChecks PRIVATE "${gamesprout_root}/Module/ScriptCompiler" "${gamesprout_root}/Module/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ScriptRecursiveFieldsChecks PRIVATE ScriptCompiler Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set_target_properties(ScriptRecursiveFieldsChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
add_test(NAME NativeScripts.RecursiveFields COMMAND ScriptRecursiveFieldsChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(NativeScripts.RecursiveFields PROPERTIES TIMEOUT 180)
