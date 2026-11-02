cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ObjectDocument LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../GameSproutRuntime/GameSproutRuntime.cmake")
# Compile the standalone third-party codec, without Foundation or Qt.
add_library(ObjectDocumentJson STATIC
    ../Foundation/Utils/JsonCpp/LunokiJsonValue.cpp
    ../Foundation/Utils/JsonCpp/LunokiJsonReader.cpp
    ../Foundation/Utils/JsonCpp/LunokiJsonWriter.cpp)
target_include_directories(ObjectDocumentJson PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../Foundation/Utils")
target_compile_features(ObjectDocumentJson PUBLIC cxx_std_20)
set_target_properties(ObjectDocumentJson PROPERTIES POSITION_INDEPENDENT_CODE ON)
file(GLOB object_document_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
add_library(ObjectDocument STATIC ${object_document_files})
add_library(ObjectDocument::ObjectDocument ALIAS ObjectDocument)
target_include_directories(ObjectDocument PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(ObjectDocument PUBLIC ObjectDocumentJson GameSproutRuntime::GameSproutRuntimeApi)
set_target_properties(ObjectDocument PROPERTIES POSITION_INDEPENDENT_CODE ON)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${object_document_files})
