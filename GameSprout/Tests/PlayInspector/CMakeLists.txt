cmake_minimum_required(VERSION 3.21)
project(PlayInspectorChecks LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(view IN ITEMS InspectorUI SceneObjectUI PhysicsUI ScriptUI AnimationUI)
    add_subdirectory("${gamesprout_root}/View/${view}" "${view}")
    set_target_properties(${view} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
endforeach()
foreach(module IN ITEMS SceneManager ObjectEditing ScriptEditor)
    add_subdirectory("${gamesprout_root}/Module/${module}" "${module}")
    set_target_properties(${module} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/out")
endforeach()
add_executable(PlayInspectorChecks PlayInspectorChecks.cpp ScriptFieldsFixture.cpp)
target_compile_features(PlayInspectorChecks PRIVATE cxx_std_20)
target_compile_options(PlayInspectorChecks PRIVATE -fno-access-control)
target_include_directories(PlayInspectorChecks PRIVATE
    "${gamesprout_root}/Tests/StaticUI" "${gamesprout_root}/View/StaticUI"
    "${gamesprout_root}/View/InspectorUI" "${gamesprout_root}/View/SceneObjectUI"
    "${gamesprout_root}/View/PhysicsUI" "${gamesprout_root}/View/ScriptUI" "${gamesprout_root}/View/AnimationUI"
    "${gamesprout_root}/Module/SceneManager" "${gamesprout_root}/Module/ObjectEditing" "${gamesprout_root}/Module/ScriptEditor"
    "${gamesprout_root}/ObjectDocument")
target_link_libraries(PlayInspectorChecks PRIVATE InspectorUI SceneObjectUI PhysicsUI ScriptUI AnimationUI SceneManager ObjectEditing ScriptEditor GameSproutInspectorControls Qt6::Widgets Qt6::Test ${CMAKE_DL_LIBS})
set(PLAY_INSPECTOR_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Runtime for isolated Play Inspector integration")
enable_testing()
add_test(NAME PlayInspector.LiveProperties COMMAND PlayInspectorChecks "${PLAY_INSPECTOR_RUNTIME_DIRECTORY}")
set_tests_properties(PlayInspector.LiveProperties PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
