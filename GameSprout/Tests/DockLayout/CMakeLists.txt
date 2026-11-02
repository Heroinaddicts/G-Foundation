cmake_minimum_required(VERSION 3.21)
project(DockLayoutChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(repository_root "${gamesprout_root}/.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_executable(DockLayoutChecks DockLayoutChecks.cpp DockDragChecks.h DockRestoreChecks.h DockVisibilityChecks.h)
target_compile_features(DockLayoutChecks PRIVATE cxx_std_20)
target_include_directories(DockLayoutChecks PRIVATE "${gamesprout_root}/Module/Interface" "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(DockLayoutChecks PRIVATE Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets Qt6::Test)
if(APPLE)
    set(binary_root "${repository_root}/Bin/GameSproutEditor.app/Contents/MacOS")
else()
    set(binary_root "${repository_root}/Bin")
endif()
set(GAMESPROUT_TEST_BINARY_ROOT "${binary_root}" CACHE PATH "Independently built editor plugin directory")
target_compile_definitions(DockLayoutChecks PRIVATE TEST_BINARY_ROOT="${GAMESPROUT_TEST_BINARY_ROOT}")
enable_testing()
add_test(NAME Docks.Headless COMMAND DockLayoutChecks)
set_tests_properties(Docks.Headless PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME Docks.Native COMMAND DockLayoutChecks)
set_tests_properties(Docks.Native PROPERTIES TIMEOUT 60)

add_executable(MenuOwnershipChecks MenuOwnershipChecks.cpp
    "${gamesprout_root}/View/IDEWindow/IDEMenuFinal.cpp"
    "${gamesprout_root}/View/IDEWindow/IDEMenuBarFinal.cpp"
    "${gamesprout_root}/View/IDEWindow/IDEWindow.ui")
set_target_properties(MenuOwnershipChecks PROPERTIES AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${gamesprout_root}/View/IDEWindow")
target_compile_features(MenuOwnershipChecks PRIVATE cxx_std_20)
target_include_directories(MenuOwnershipChecks PRIVATE
    "${gamesprout_root}/View/IDEWindow" "${gamesprout_root}/View/Interface"
    "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(MenuOwnershipChecks PRIVATE Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Widgets)
add_test(NAME Menus.DesignerOwnership COMMAND MenuOwnershipChecks)
set_tests_properties(Menus.DesignerOwnership PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
