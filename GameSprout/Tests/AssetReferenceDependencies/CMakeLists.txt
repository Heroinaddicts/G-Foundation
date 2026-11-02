cmake_minimum_required(VERSION 3.21)
project(AssetReferenceDependenciesChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
add_subdirectory("${gamesprout_root}/Module/LibraryManager" LibraryManager)
set_target_properties(LibraryManager PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Products")
add_executable(AssetReferenceDependenciesChecks AssetReferenceDependenciesChecks.cpp)
target_compile_features(AssetReferenceDependenciesChecks PRIVATE cxx_std_20)
target_compile_options(AssetReferenceDependenciesChecks PRIVATE -fno-access-control)
target_include_directories(AssetReferenceDependenciesChecks PRIVATE
    "${gamesprout_root}/Module/LibraryManager" "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(AssetReferenceDependenciesChecks PRIVATE LibraryManager Qt6::Widgets
    Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi)
enable_testing()
add_test(NAME Assets.StableGuidDependencies COMMAND AssetReferenceDependenciesChecks)
