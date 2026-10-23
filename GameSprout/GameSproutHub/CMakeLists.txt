cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

project(GameSproutHub VERSION 0.1 LANGUAGES CXX)

# Public APIs and prebuilt runtime only; Foundation is built independently.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationSDK.cmake")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        HubWindow.cpp
        HubWindow.h
        HubWindow.ui
        HubProjectManager.cpp
        HubProjectManager.h
        NewProjectDialog.cpp
        NewProjectDialog.h
        NewProjectDialog.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(GameSproutHub
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET GameSproutHub APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(GameSproutHub SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(GameSproutHub
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_sources(GameSproutHub PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${PROJECT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)

target_link_libraries(GameSproutHub PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Foundation::FoundationApi
    Foundation::Utils
    ${CMAKE_DL_LIBS}
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.GameSproutHub)
endif()
set_target_properties(GameSproutHub PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# The Hub locates GameSprout beside its own app bundle/executable.
get_filename_component(GAMESPROUT_HUB_OUTPUT_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Bin" ABSOLUTE
)
set_target_properties(GameSproutHub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GAMESPROUT_HUB_OUTPUT_DIRECTORY}"
)
foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${configuration}" configuration_upper)
    set_property(TARGET GameSproutHub PROPERTY
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${GAMESPROUT_HUB_OUTPUT_DIRECTORY}"
    )
endforeach()

# Refresh the independently built runtime even when the Hub does not relink.
add_custom_target(GameSproutHubDeployFoundation
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GameSproutHub>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Foundation::Foundation>"
        "$<TARGET_FILE_DIR:GameSproutHub>/$<TARGET_FILE_NAME:Foundation::Foundation>"
    DEPENDS "$<TARGET_FILE:Foundation::Foundation>"
    COMMENT "Copying Foundation runtime into GameSproutHub"
    VERBATIM
)
add_dependencies(GameSproutHub GameSproutHubDeployFoundation)

include(GNUInstallDirs)
install(TARGETS GameSproutHub
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(GameSproutHub)
endif()
