cmake_minimum_required(VERSION 3.21)
project(StaticUIChecks LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${gamesprout_root}/View/StaticUI" StaticUI)
add_subdirectory("${gamesprout_root}/View/SceneObjectUI" SceneObjectUI)
add_subdirectory("${gamesprout_root}/View/InspectorUI" InspectorUI)
add_subdirectory("${gamesprout_root}/Module/SceneManager" SceneManager)
add_subdirectory("${gamesprout_root}/Module/StaticAuthoring" StaticAuthoring)
# Integration checks must never replace the Editor's loaded plugins.
set_target_properties(StaticUI SceneObjectUI InspectorUI SceneManager StaticAuthoring PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
add_executable(StaticUIChecks StaticUIChecks.cpp StaticUIFixtures.h StaticAuthoringFixture.cpp)
target_compile_features(StaticUIChecks PRIVATE cxx_std_20)
target_include_directories(StaticUIChecks PRIVATE
    "${gamesprout_root}/View/StaticUI" "${gamesprout_root}/View/SceneObjectUI"
    "${gamesprout_root}/View/InspectorUI" "${gamesprout_root}/Module/SceneManager"
    "${gamesprout_root}/Module/StaticAuthoring" "${gamesprout_root}/ObjectDocument")
target_link_libraries(StaticUIChecks PRIVATE StaticUI SceneObjectUI InspectorUI SceneManager StaticAuthoring
    GameSproutInspectorControls Qt6::Widgets Qt6::Test ${CMAKE_DL_LIBS})
set(STATIC_UI_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Runtime directory for Static UI checks")
enable_testing()
add_test(NAME StaticUI.InspectorHeaderAndTransactions COMMAND StaticUIChecks "${STATIC_UI_RUNTIME_DIRECTORY}")
set_tests_properties(StaticUI.InspectorHeaderAndTransactions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
