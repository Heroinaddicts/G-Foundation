cmake_minimum_required(VERSION 3.21)
project(AssetOperationsChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/ParallelBuild.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
foreach(module IN ITEMS AssetsManager AssetOperations RuntimeAssets)
 add_subdirectory("${gamesprout_root}/Module/${module}" "${module}")
 set_target_properties(${module} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products")
endforeach()
foreach(view IN ITEMS AssetOperationsUI ProjectAssetsUI)
 add_subdirectory("${gamesprout_root}/View/${view}" "${view}")
 set_target_properties(${view} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products")
endforeach()
add_executable(AssetDeletionChecks AssetDeletionChecks.cpp BrowserFixture.cpp DeleteViewFixture.cpp)
target_compile_features(AssetDeletionChecks PRIVATE cxx_std_20)
target_compile_options(AssetDeletionChecks PRIVATE -fno-access-control)
target_include_directories(AssetDeletionChecks PRIVATE
 "${gamesprout_root}/Module/AssetsManager" "${gamesprout_root}/Module/AssetOperations"
 "${gamesprout_root}/Module/RuntimeAssets" "${gamesprout_root}/View/AssetOperationsUI"
 "${gamesprout_root}/View/ProjectAssetsUI" "${gamesprout_root}/Module/Interface"
 "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(AssetDeletionChecks PRIVATE AssetsManager AssetOperations RuntimeAssets AssetOperationsUI ProjectAssetsUI
 Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test)
enable_testing()
add_test(NAME AssetOperations.DeletionAndReferences COMMAND AssetDeletionChecks "${gamesprout_root}/../Bin")
set_tests_properties(AssetOperations.DeletionAndReferences PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
