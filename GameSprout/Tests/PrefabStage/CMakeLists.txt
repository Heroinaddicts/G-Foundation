cmake_minimum_required(VERSION 3.21)
project(PrefabStageChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
include("${gamesprout_root}/ObjectDocument/ObjectDocument.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
foreach(module IN ITEMS SceneManager PrefabManager PrefabStage)
    add_subdirectory("${gamesprout_root}/Module/${module}" "${module}")
    set_target_properties(${module} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
endforeach()
add_executable(PrefabStageChecks PrefabStageChecks.cpp PrefabStageTestSupport.h PrefabStageScriptChecks.h LargeDocumentChecks.h)
target_compile_features(PrefabStageChecks PRIVATE cxx_std_20)
target_include_directories(PrefabStageChecks PRIVATE "${gamesprout_root}/Module/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PrefabStageChecks PRIVATE Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi
    ObjectDocument::ObjectDocument Qt6::Core ${CMAKE_DL_LIBS})
target_compile_definitions(PrefabStageChecks PRIVATE SCENE_MODULE_FILE="$<TARGET_FILE:SceneManager>"
    PREFAB_MODULE_FILE="$<TARGET_FILE:PrefabManager>" STAGE_MODULE_FILE="$<TARGET_FILE:PrefabStage>")
add_dependencies(PrefabStageChecks SceneManager PrefabManager PrefabStage)
set(PREFAB_STAGE_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Independent Runtime SDK directory")
enable_testing()
add_test(NAME PrefabStage.IsolatedEditingAndSourcePropagation COMMAND PrefabStageChecks "${PREFAB_STAGE_RUNTIME_DIRECTORY}")
set_tests_properties(PrefabStage.IsolatedEditingAndSourcePropagation PROPERTIES TIMEOUT 60)
add_test(NAME PrefabStage.LargeSceneAndPrefabDocuments COMMAND PrefabStageChecks "${PREFAB_STAGE_RUNTIME_DIRECTORY}" --large-documents)
set_tests_properties(PrefabStage.LargeSceneAndPrefabDocuments PROPERTIES TIMEOUT 180)
