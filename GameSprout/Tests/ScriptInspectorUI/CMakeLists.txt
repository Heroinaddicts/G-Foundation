cmake_minimum_required(VERSION 3.21)
project(ScriptInspectorUIChecks LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../View/SceneObjectUI" SceneObjectUI)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../View/ScriptUI" ScriptUI)
# These checks never deploy to the user's running Editor.
set_target_properties(SceneObjectUI ScriptUI PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
add_executable(ScriptInspectorUIChecks ScriptInspectorUIChecks.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeModel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeView.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeView.h")
set_target_properties(ScriptInspectorUIChecks PROPERTIES AUTOMOC ON)
target_compile_features(ScriptInspectorUIChecks PRIVATE cxx_std_20)
target_include_directories(ScriptInspectorUIChecks PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../View/SceneObjectUI"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/ScriptUI")
target_link_libraries(ScriptInspectorUIChecks PRIVATE SceneObjectUI ScriptUI
    GameSproutInspectorControls Qt6::Widgets Qt6::Test)
enable_testing()
add_test(NAME ScriptInspectorUIChecks COMMAND ScriptInspectorUIChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(ScriptInspectorUIChecks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(HierarchyObjectDragChecks
    HierarchyObjectDragChecks.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeModel.cpp")
target_compile_features(HierarchyObjectDragChecks PRIVATE cxx_std_20)
target_include_directories(HierarchyObjectDragChecks PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI")
target_link_libraries(HierarchyObjectDragChecks PRIVATE
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets ${CMAKE_DL_LIBS})
add_test(NAME HierarchyObjectDragChecks COMMAND HierarchyObjectDragChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")

add_executable(HierarchySelectionGestureChecks
    HierarchySelectionGestureChecks.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeModel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeView.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI/HierarchyTreeView.h")
set_target_properties(HierarchySelectionGestureChecks PROPERTIES AUTOMOC ON)
target_compile_features(HierarchySelectionGestureChecks PRIVATE cxx_std_20)
target_include_directories(HierarchySelectionGestureChecks PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../View/HierarchyUI")
target_link_libraries(HierarchySelectionGestureChecks PRIVATE
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test ${CMAKE_DL_LIBS})
add_test(NAME HierarchySelectionGestureChecks COMMAND HierarchySelectionGestureChecks "${SCRIPT_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(HierarchySelectionGestureChecks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
