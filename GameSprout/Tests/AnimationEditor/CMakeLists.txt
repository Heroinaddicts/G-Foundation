cmake_minimum_required(VERSION 3.21)
project(AnimationEditorChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(repository_root "${gamesprout_root}/.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(GAMESPROUT_TEST_RUNTIME_DIRECTORY "${repository_root}/Bin" CACHE PATH "Independently built Runtime SDK directory")
file(GLOB viewport_sources CONFIGURE_DEPENDS
    "${gamesprout_root}/View/SceneViewUI/SceneViewport*.cpp"
    "${gamesprout_root}/View/SceneViewUI/SceneGizmo*.cpp")
add_executable(AnimationEditorChecks AnimationEditorChecks.cpp
    ${viewport_sources}
    "${gamesprout_root}/View/SceneViewUI/SceneViewport.h"
    "${gamesprout_root}/View/SceneViewUI/SceneViewCamera.cpp"
    "${gamesprout_root}/View/SceneViewUI/SceneViewUI.ui"
    "${gamesprout_root}/View/GameViewUI/GameViewport.cpp"
    "${gamesprout_root}/View/GameViewUI/GameViewport.h"
    "${gamesprout_root}/View/GameViewUI/GameViewUI.ui")
target_compile_features(AnimationEditorChecks PRIVATE cxx_std_20)
target_include_directories(AnimationEditorChecks PRIVATE
    "${gamesprout_root}/View/SceneViewUI" "${gamesprout_root}/View/GameViewUI"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/Module/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(AnimationEditorChecks PRIVATE Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets)
target_compile_definitions(AnimationEditorChecks PRIVATE TEST_RUNTIME_ROOT="${GAMESPROUT_TEST_RUNTIME_DIRECTORY}")
set_target_properties(AnimationEditorChecks PROPERTIES AUTOMOC ON AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${gamesprout_root}/View/SceneViewUI;${gamesprout_root}/View/GameViewUI")
enable_testing()
add_test(NAME AnimationEditor.HeadlessViewIsolation COMMAND AnimationEditorChecks)
set_tests_properties(AnimationEditor.HeadlessViewIsolation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_test(NAME AnimationEditor.NativeSurfaceLifecycle COMMAND AnimationEditorChecks --native)
set_tests_properties(AnimationEditor.NativeSurfaceLifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
