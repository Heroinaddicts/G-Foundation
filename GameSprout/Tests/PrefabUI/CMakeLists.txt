cmake_minimum_required(VERSION 3.21)
project(PrefabUIChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(PrefabUIChecks PrefabUIChecks.cpp
    "${gamesprout_root}/View/PrefabUI/PrefabUI.cpp"
    "${gamesprout_root}/View/PrefabUI/PrefabUI.h"
    "${gamesprout_root}/View/PrefabUI/PrefabInstanceSection.cpp"
    "${gamesprout_root}/View/PrefabUI/PrefabInstanceSection.ui"
    "${gamesprout_root}/View/PrefabUI/PrefabOverridesPopup.ui"
    "${gamesprout_root}/View/PrefabUI/PrefabAssetInspector.ui"
    "${gamesprout_root}/View/PrefabUI/PrefabMessageDialog.ui"
    "${gamesprout_root}/View/PrefabUI/PrefabActions.ui"
    "${gamesprout_root}/View/PrefabUI/PrefabSaveDialog.ui")
target_compile_features(PrefabUIChecks PRIVATE cxx_std_20)
target_include_directories(PrefabUIChecks PRIVATE "${gamesprout_root}/View/PrefabUI"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/Module/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PrefabUIChecks PRIVATE Qt6::Widgets Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi)
set_target_properties(PrefabUIChecks PROPERTIES AUTOMOC ON AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${gamesprout_root}/View/PrefabUI")
enable_testing()
add_test(NAME PrefabUI.EventDrivenAuthoring COMMAND PrefabUIChecks)
set_tests_properties(PrefabUI.EventDrivenAuthoring PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
