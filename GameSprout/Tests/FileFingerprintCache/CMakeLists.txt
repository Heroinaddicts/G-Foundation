cmake_minimum_required(VERSION 3.21)
project(FileFingerprintCacheChecks LANGUAGES CXX)
enable_testing()

get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

add_executable(FileFingerprintCacheChecks
    FileFingerprintCacheChecks.cpp
    "${gamesprout_root}/Module/AssetsPipeline/AssetFileFingerprintCache.cpp")
target_compile_features(FileFingerprintCacheChecks PRIVATE cxx_std_20)
target_include_directories(FileFingerprintCacheChecks PRIVATE
    "${gamesprout_root}/Module/AssetsPipeline")
target_link_libraries(FileFingerprintCacheChecks PRIVATE
    Foundation::FoundationApi Qt6::Core Threads::Threads)

add_test(NAME Assets.FileFingerprintCache COMMAND FileFingerprintCacheChecks)
set_tests_properties(Assets.FileFingerprintCache PROPERTIES TIMEOUT 30)
