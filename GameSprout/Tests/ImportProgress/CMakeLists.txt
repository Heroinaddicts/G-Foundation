cmake_minimum_required(VERSION 3.21)
project(ImportProgressChecks LANGUAGES CXX)
enable_testing()

get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GRHI/GRHI.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(ImportProgressChecks ImportProgressChecks.cpp)
target_compile_features(ImportProgressChecks PRIVATE cxx_std_20)
target_include_directories(ImportProgressChecks PRIVATE
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ImportProgressChecks PRIVATE
    Foundation::FoundationApi GRHI::GRHIApi Qt6::Widgets)

if(APPLE)
    set(default_ide_library "${gamesprout_root}/../Bin/GameSproutEditor.app/Contents/MacOS/Logic/View/libIDEWindow.dylib")
elseif(WIN32)
    set(default_ide_library "${gamesprout_root}/../Bin/Logic/View/IDEWindow.dll")
else()
    set(default_ide_library "${gamesprout_root}/../Bin/Logic/View/libIDEWindow.so")
endif()
set(GAMESPROUT_IDE_LIBRARY "${default_ide_library}" CACHE FILEPATH "IDEWindow runtime to test")
add_test(NAME IDE.ImportProgress COMMAND ImportProgressChecks "${GAMESPROUT_IDE_LIBRARY}")
set_tests_properties(IDE.ImportProgress PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(PipelineProgressChecks PipelineProgressChecks.cpp)
target_compile_features(PipelineProgressChecks PRIVATE cxx_std_20)
target_include_directories(PipelineProgressChecks PRIVATE
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PipelineProgressChecks PRIVATE
    Foundation::FoundationApi GRHI::GRHIApi Qt6::Widgets)
if(APPLE)
    set(pipeline_test_binary_root "${gamesprout_root}/../Bin/GameSproutEditor.app/Contents/MacOS")
else()
    set(pipeline_test_binary_root "${gamesprout_root}/../Bin")
endif()
target_compile_definitions(PipelineProgressChecks PRIVATE TEST_BINARY_ROOT="${pipeline_test_binary_root}")
add_test(NAME Assets.PipelineProgress COMMAND PipelineProgressChecks)
set_tests_properties(Assets.PipelineProgress PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
