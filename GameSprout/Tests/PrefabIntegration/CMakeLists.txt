cmake_minimum_required(VERSION 3.21)
project(PrefabIntegrationChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
include("${gamesprout_root}/ObjectDocument/ObjectDocument.cmake")
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
add_subdirectory("${gamesprout_root}/Module/SceneManager" SceneManager)
add_subdirectory("${gamesprout_root}/Module/PrefabManager" PrefabManager)
# A test build must not publish plugins or Runtime binaries into the editor installation.
foreach(module IN ITEMS SceneManager PrefabManager)
    set_target_properties(${module} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
endforeach()
add_executable(PrefabIntegrationChecks PrefabIntegrationChecks.cpp)
target_compile_features(PrefabIntegrationChecks PRIVATE cxx_std_20)
target_include_directories(PrefabIntegrationChecks PRIVATE
    "${gamesprout_root}/Module/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PrefabIntegrationChecks PRIVATE Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi ObjectDocument::ObjectDocument Qt${QT_VERSION_MAJOR}::Core ${CMAKE_DL_LIBS})
target_compile_definitions(PrefabIntegrationChecks PRIVATE
    SCENE_MODULE_FILE="$<TARGET_FILE:SceneManager>" PREFAB_MODULE_FILE="$<TARGET_FILE:PrefabManager>")
add_dependencies(PrefabIntegrationChecks SceneManager PrefabManager)
set(PREFAB_TEST_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Directory of the independently built Runtime SDK under test")
enable_testing()
add_test(NAME PrefabIntegration.AuthoringAndInstances COMMAND PrefabIntegrationChecks "${PREFAB_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(PrefabIntegration.AuthoringAndInstances PROPERTIES TIMEOUT 60)
