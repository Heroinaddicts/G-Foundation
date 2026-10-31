cmake_minimum_required(VERSION 3.21)
project(LoadingBudgetChecks LANGUAGES CXX)
enable_testing()

get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GRHI/GRHI.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(LoadingBudgetChecks LoadingBudgetChecks.cpp)
target_compile_features(LoadingBudgetChecks PRIVATE cxx_std_20)
target_include_directories(LoadingBudgetChecks PRIVATE
    "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(LoadingBudgetChecks PRIVATE
    Foundation::FoundationApi GRHI::GRHIApi Qt6::Widgets)

if(APPLE)
    set(default_ide_library "${gamesprout_root}/../Bin/GameSproutEditor.app/Contents/MacOS/Logic/View/libIDEWindow.dylib")
elseif(WIN32)
    set(default_ide_library "${gamesprout_root}/../Bin/Logic/View/IDEWindow.dll")
else()
    set(default_ide_library "${gamesprout_root}/../Bin/Logic/View/libIDEWindow.so")
endif()
set(GAMESPROUT_IDE_LIBRARY "${default_ide_library}" CACHE FILEPATH "IDEWindow runtime to test")
add_test(NAME IDE.LoadingCompletionBudget COMMAND LoadingBudgetChecks "${GAMESPROUT_IDE_LIBRARY}")
set_tests_properties(IDE.LoadingCompletionBudget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
