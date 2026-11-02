cmake_minimum_required(VERSION 3.21)
project(ShaderAuthoringChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_subdirectory("${gamesprout_root}/Module/ShaderAuthoring" ShaderAuthoring)
add_subdirectory("${gamesprout_root}/View/ShaderUI" ShaderUI)
set_target_properties(ShaderAuthoring ShaderUI PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
add_executable(ShaderAuthoringChecks ShaderAuthoringChecks.cpp)
target_compile_features(ShaderAuthoringChecks PRIVATE cxx_std_20)
target_include_directories(ShaderAuthoringChecks PRIVATE
    "${gamesprout_root}/Module/ShaderAuthoring" "${gamesprout_root}/View/ShaderUI"
    "${gamesprout_root}/Module/Interface" "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ShaderAuthoringChecks PRIVATE ShaderAuthoring ShaderUI Foundation::FoundationApi Qt6::Widgets Qt6::Test GameSproutRuntime::GameSproutRuntimeApi)
enable_testing()
add_test(NAME Shader.AuthoringAndCreationUI COMMAND ShaderAuthoringChecks)
set_tests_properties(Shader.AuthoringAndCreationUI PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

add_subdirectory("${gamesprout_root}/View/MaterialUI" MaterialUI)
set_target_properties(MaterialUI PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
add_executable(MaterialAdvancedOptionsChecks MaterialAdvancedOptionsChecks.cpp)
target_compile_features(MaterialAdvancedOptionsChecks PRIVATE cxx_std_20)
target_include_directories(MaterialAdvancedOptionsChecks PRIVATE
    "${gamesprout_root}/View/MaterialUI" "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(MaterialAdvancedOptionsChecks PRIVATE MaterialUI GameSproutInspectorControls
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test)
add_test(NAME Material.AdvancedOptionsDesigner COMMAND MaterialAdvancedOptionsChecks)
set_tests_properties(Material.AdvancedOptionsDesigner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
