cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../Foundation/ParallelBuild.cmake")

# Directory queries below must not make deployment depend on the launcher.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

project(GameSproutEditor VERSION 0.1 LANGUAGES CXX)

get_filename_component(GAMESPROUT_OUTPUT_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Bin" ABSOLUTE
)
# Public APIs and independently built binaries only.
include("${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/FoundationSDK.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../GRHI/GRHI.cmake")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        GameSproutFinal.cpp
        GameSproutFinal.h
        GameSproutEditorFinal.cpp
        GameSproutEditorFinal.h
        Api/GameSproutApi.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(GameSproutEditor
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET GameSproutEditor APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(GameSproutEditor SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(GameSproutEditor
            ${PROJECT_SOURCES}
        )
    endif()
endif()


# Mirror the on-disk layout instead of grouping by file extension.
target_sources(GameSproutEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)
get_target_property(GAMESPROUT_SOURCES GameSproutEditor SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${GAMESPROUT_SOURCES}
)

target_include_directories(GameSproutEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Api"
)
# GRHI is loaded explicitly through ExportGRHIApi, rather than linked here.
target_link_libraries(GameSproutEditor PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Foundation::FoundationApi
    Foundation::Utils
    GRHI::GRHIApi
    ${CMAKE_DL_LIBS}
)

set_target_properties(GameSproutEditor PROPERTIES
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
        set_property(TARGET GameSproutEditor PROPERTY
            "${output_kind}_OUTPUT_DIRECTORY_${configuration_upper}"
            "${GAMESPROUT_OUTPUT_DIRECTORY}"
        )
    endforeach()
endforeach()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.GameSproutEditor)
endif()
set_target_properties(GameSproutEditor PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Custom targets run on every build, even when GameSproutEditor needs no relinking.
# Deploy the separately built GRHI binary alongside the executable.
get_target_property(grhi_runtime_dependencies GRHI::GRHI GRHI_RUNTIME_DEPENDENCIES)
add_custom_target(GameSproutDeployGRHI
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GameSproutEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:GRHI::GRHI>"
        "$<TARGET_FILE_DIR:GameSproutEditor>/$<TARGET_FILE_NAME:GRHI::GRHI>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${grhi_runtime_dependencies}
        "$<TARGET_FILE_DIR:GameSproutEditor>"
    DEPENDS "$<TARGET_FILE:GRHI::GRHI>" ${grhi_runtime_dependencies}
    COMMENT "Copying GRHI runtime into GameSproutEditor"
    VERBATIM
)
add_dependencies(GameSproutEditor GameSproutDeployGRHI)

add_custom_target(GameSproutDeployFoundation
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:GameSproutEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Foundation::Foundation>"
        "$<TARGET_FILE_DIR:GameSproutEditor>/$<TARGET_FILE_NAME:Foundation::Foundation>"
    DEPENDS "$<TARGET_FILE:Foundation::Foundation>"
    COMMENT "Copying Foundation runtime into GameSproutEditor"
    VERBATIM
)
add_dependencies(GameSproutEditor GameSproutDeployFoundation)

include(GNUInstallDirs)
install(TARGETS GameSproutEditor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Build and deploy every plugin together with the launcher, including when
# Qt Creator builds only the GameSproutEditor target after cleaning its app bundle.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Module" "${CMAKE_CURRENT_BINARY_DIR}/Module")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../View" "${CMAKE_CURRENT_BINARY_DIR}/View")

function(GameSproutAddPluginDependencies plugin_directory)
    get_property(plugin_targets DIRECTORY "${plugin_directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(plugin_target IN LISTS plugin_targets)
        get_target_property(plugin_type "${plugin_target}" TYPE)
        if(plugin_type STREQUAL "SHARED_LIBRARY" OR plugin_type STREQUAL "MODULE_LIBRARY")
            add_dependencies(GameSproutEditor "${plugin_target}")
        endif()
    endforeach()
    get_property(plugin_subdirectories DIRECTORY "${plugin_directory}" PROPERTY SUBDIRECTORIES)
    foreach(plugin_subdirectory IN LISTS plugin_subdirectories)
        GameSproutAddPluginDependencies("${plugin_subdirectory}")
    endforeach()
endfunction()
GameSproutAddPluginDependencies("${CMAKE_CURRENT_SOURCE_DIR}/../Module")
GameSproutAddPluginDependencies("${CMAKE_CURRENT_SOURCE_DIR}/../View")

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(GameSproutEditor)
endif()
