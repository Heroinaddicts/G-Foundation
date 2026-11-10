cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../Foundation/ParallelBuild.cmake")

# Directory queries below must not make deployment depend on the launcher.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

project(GameSproutAgent VERSION 0.1 LANGUAGES CXX)

get_filename_component(GAMESPROUT_OUTPUT_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Bin" ABSOLUTE
)
# Public APIs and independently built binaries only.
include("${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/FoundationSDK.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../GameSproutRuntime/GameSproutRuntime.cmake")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(PROJECT_SOURCES
        main.cpp
        MainWindow.cpp
        MainWindow.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(GameSproutAgent
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(GameSproutAgent SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(GameSproutAgent
            ${PROJECT_SOURCES}
        )
    endif()
endif()


# Mirror the on-disk layout instead of grouping by file extension.
target_sources(GameSproutAgent PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)
get_target_property(GAMESPROUT_SOURCES GameSproutAgent SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${GAMESPROUT_SOURCES}
)

target_include_directories(GameSproutAgent PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Api"
)
# The editor consumes only Runtime public interfaces for rendering.
target_link_libraries(GameSproutAgent PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Foundation::FoundationApi
    Foundation::Utils
    ${CMAKE_DL_LIBS}
)

set_target_properties(GameSproutAgent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GAMESPROUT_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${GAMESPROUT_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${GAMESPROUT_OUTPUT_DIRECTORY}"
    PDB_OUTPUT_DIRECTORY "${GAMESPROUT_OUTPUT_DIRECTORY}"
    COMPILE_PDB_OUTPUT_DIRECTORY "${GAMESPROUT_OUTPUT_DIRECTORY}"
)

# Keep multi-configuration generators from appending Debug/Release to Bin.
foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${configuration}" configuration_upper)
    foreach(output_kind IN ITEMS RUNTIME LIBRARY ARCHIVE PDB COMPILE_PDB)
        set_property(TARGET GameSproutAgent PROPERTY
            "${output_kind}_OUTPUT_DIRECTORY_${configuration_upper}"
            "${GAMESPROUT_OUTPUT_DIRECTORY}"
        )
    endforeach()
endforeach()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.GameSproutAgent)
endif()
set_target_properties(GameSproutAgent PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Copy Foundation and the independently built Runtime SDK beside the shell.
add_custom_target(GameSproutDeployFoundation
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GameSproutAgent>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Foundation::Foundation>"
        "$<TARGET_FILE_DIR:GameSproutAgent>/$<TARGET_FILE_NAME:Foundation::Foundation>"
    DEPENDS "$<TARGET_FILE:Foundation::Foundation>"
    COMMENT "Copying Foundation runtime into GameSproutAgent"
    VERBATIM
)
add_dependencies(GameSproutAgent GameSproutDeployFoundation)

GameSproutRuntimeDeploy(GameSproutAgent)

include(GNUInstallDirs)
install(TARGETS GameSproutAgent
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(GameSproutAgent)
endif()
