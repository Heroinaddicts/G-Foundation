cmake_minimum_required(VERSION 3.21)
project(PlayModeChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
foreach(module IN ITEMS RuntimeHost SceneManager AnimationPreview PlayMode)
    add_subdirectory("${gamesprout_root}/Module/${module}" "${module}")
    set_target_properties(${module} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
endforeach()
add_executable(PlayModeChecks PlayModeChecks.cpp)
target_compile_features(PlayModeChecks PRIVATE cxx_std_20)
target_include_directories(PlayModeChecks PRIVATE
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/Module/RuntimeHost"
    "${gamesprout_root}/Module/SceneManager"
    "${gamesprout_root}/Module/AnimationPreview"
    "${gamesprout_root}/Module/PlayMode"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PlayModeChecks PRIVATE RuntimeHost SceneManager AnimationPreview PlayMode
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Core)
set_target_properties(PlayModeChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products/$<0:>")
set(PLAY_TEST_RUNTIME_DIRECTORY "${gamesprout_root}/../Bin" CACHE PATH "Independently built Runtime directory")
enable_testing()
add_test(NAME Editor.PlayModeSession COMMAND PlayModeChecks "${PLAY_TEST_RUNTIME_DIRECTORY}")
set_tests_properties(Editor.PlayModeSession PROPERTIES TIMEOUT 30)
