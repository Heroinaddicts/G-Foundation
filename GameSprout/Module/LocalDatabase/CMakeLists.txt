cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/../../Foundation/ParallelBuild.cmake")

# Querying the launcher's output directory must not make the plugin depend
# on the launcher; the launcher already depends on its plugins.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(LocalDatabase VERSION 0.1 LANGUAGES CXX)
endif()

# LMDB is compiled as C, including when this module is added to a CXX-only parent.
enable_language(C)
add_subdirectory(ThirdParty/LMDB)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../Foundation/FoundationApi.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../GRHI/GRHI.cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_library(LocalDatabase SHARED
    Header.h
    ExportModule.cpp
    LocalDatabase.cpp
    LocalDatabase.h
    LDBSession.cpp
    LDBSession.hpp
    LDBTable.cpp
    LDBTable.hpp
    CMakeLists.txt
)
target_compile_features(LocalDatabase PRIVATE cxx_std_20)
target_link_libraries(LocalDatabase PRIVATE
    Foundation::FoundationApi
    GRHI::GRHIApi
    Qt${QT_VERSION_MAJOR}::Core
    LMDB::LMDB
)
target_include_directories(LocalDatabase PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../View/Interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../GameSproutEditor/Api"
)
set_target_properties(LocalDatabase PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Header.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ExportModule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LocalDatabase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LocalDatabase.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/LDBSession.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LDBSession.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LDBTable.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LDBTable.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(GLOB_RECURSE rhi_adapater_interface_files CONFIGURE_DEPENDS
        LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/../Interface/*"
    )
    target_sources(LocalDatabase PRIVATE ${rhi_adapater_interface_files})
    get_filename_component(module_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    source_group(TREE "${module_root}" FILES ${rhi_adapater_interface_files})
endif()

get_filename_component(gamesprout_bin_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Bin" ABSOLUTE
)
if(TARGET GameSproutEditor)
    set(module_output_directory "$<TARGET_FILE_DIR:GameSproutEditor>/Logic/Module")
elseif(APPLE)
    set(module_output_directory
        "${gamesprout_bin_directory}/GameSproutEditor.app/Contents/MacOS/Logic/Module")
else()
    set(module_output_directory "${gamesprout_bin_directory}/Logic/Module")
endif()

set_target_properties(LocalDatabase PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    LIBRARY_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    ARCHIVE_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
    COMPILE_PDB_OUTPUT_DIRECTORY "${module_output_directory}/$<0:>"
)

option(LOCALDATABASE_BUILD_TESTS "Build LocalDatabase interface tests" OFF)
if(LOCALDATABASE_BUILD_TESTS)
    enable_testing()
    add_executable(LocalDatabaseTests Tests/LocalDatabaseTests.cpp)
    target_compile_features(LocalDatabaseTests PRIVATE cxx_std_20)
    target_include_directories(LocalDatabaseTests PRIVATE
        $<TARGET_PROPERTY:LocalDatabase,INCLUDE_DIRECTORIES>
    )
    target_link_libraries(LocalDatabaseTests PRIVATE
        LocalDatabase
        Foundation::FoundationApi
        GRHI::GRHIApi
        Qt${QT_VERSION_MAJOR}::Core
    )
    set_target_properties(LocalDatabaseTests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    add_test(NAME LocalDatabase.Interfaces COMMAND LocalDatabaseTests)
endif()
