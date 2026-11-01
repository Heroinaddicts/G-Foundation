include_guard(GLOBAL)
if(NOT TARGET GameSproutInspectorControls)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../Foundation/FoundationApi.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/../../../GameSproutRuntime/GameSproutRuntime.cmake")
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
    file(GLOB_RECURSE inspector_control_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.ui" "${CMAKE_CURRENT_LIST_DIR}/*.qrc")
    add_library(GameSproutInspectorControls STATIC ${inspector_control_files})
    target_compile_features(GameSproutInspectorControls PUBLIC cxx_std_20)
    target_include_directories(GameSproutInspectorControls PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../Interface"
        "${CMAKE_CURRENT_LIST_DIR}/../../../Module/Interface"
        "${CMAKE_CURRENT_LIST_DIR}/../../../GameSproutEditor/Api")
    target_link_libraries(GameSproutInspectorControls PUBLIC Qt${QT_VERSION_MAJOR}::Widgets
        Foundation::FoundationApi GameSproutRuntime::GameSproutRuntimeApi)
    set_target_properties(GameSproutInspectorControls PROPERTIES
        POSITION_INDEPENDENT_CODE ON AUTOMOC ON AUTOUIC ON AUTORCC ON)
    source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${inspector_control_files})
endif()
