cmake_minimum_required(VERSION 3.21)
project(LightingAuthoringChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
include("${gamesprout_root}/ObjectDocument/ObjectDocument.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
foreach(feature IN ITEMS LightingAuthoring ObjectEditing SceneManager)
    add_subdirectory("${gamesprout_root}/Module/${feature}" "${feature}")
    set_target_properties(${feature} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
endforeach()
add_subdirectory("${gamesprout_root}/View/LightingUI" LightingUI)
set_target_properties(LightingUI PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
add_executable(LightingAuthoringChecks LightingAuthoringChecks.cpp)
target_compile_features(LightingAuthoringChecks PRIVATE cxx_std_20)
target_include_directories(LightingAuthoringChecks PRIVATE
    "${gamesprout_root}/Module/LightingAuthoring" "${gamesprout_root}/Module/ObjectEditing"
    "${gamesprout_root}/Module/SceneManager" "${gamesprout_root}/View/LightingUI"
    "${gamesprout_root}/Module/Interface" "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(LightingAuthoringChecks PRIVATE LightingAuthoring ObjectEditing SceneManager LightingUI
    ObjectDocument::ObjectDocument GameSproutInspectorControls Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test)
enable_testing()
add_test(NAME Lighting.AuthoringTransactionsAndInspector COMMAND LightingAuthoringChecks "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Lighting.AuthoringTransactionsAndInspector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_executable(EnvironmentLightingWindowChecks EnvironmentLightingWindowChecks.cpp)
target_compile_features(EnvironmentLightingWindowChecks PRIVATE cxx_std_20)
target_include_directories(EnvironmentLightingWindowChecks PRIVATE
    "${gamesprout_root}/View/LightingUI" "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(EnvironmentLightingWindowChecks PRIVATE LightingUI Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test)
add_test(NAME Lighting.EnvironmentDesignerWindow COMMAND EnvironmentLightingWindowChecks)
set_tests_properties(Lighting.EnvironmentDesignerWindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
