# Internal source-build entry; SDK consumers include Utils.cmake instead.
include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/UtilsApi.cmake")

set(FOUNDATION_JSON_CPP_IMPLEMENTATION_FILES
    "${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonReader.cpp"
    "${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonValue.cpp"
    "${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonWriter.cpp"
)

if(NOT TARGET FoundationUtilsJson)
    add_library(FoundationUtilsJson INTERFACE)
    add_library(Foundation::UtilsJson ALIAS FoundationUtilsJson)
    set_target_properties(FoundationUtilsJson PROPERTIES
        EXPORT_NAME UtilsJson
    )
    target_compile_features(FoundationUtilsJson INTERFACE cxx_std_20)
    target_include_directories(FoundationUtilsJson INTERFACE
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:SDK/Include/Foundation/Utils>"
    )
    target_sources(FoundationUtilsJson INTERFACE
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonReader.cpp>"
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonValue.cpp>"
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}/JsonCpp/LunokiJsonWriter.cpp>"
        "$<INSTALL_INTERFACE:SDK/Source/Foundation/Utils/JsonCpp/FoundationJsonReader.cpp>"
        "$<INSTALL_INTERFACE:SDK/Source/Foundation/Utils/JsonCpp/FoundationJsonValue.cpp>"
        "$<INSTALL_INTERFACE:SDK/Source/Foundation/Utils/JsonCpp/FoundationJsonWriter.cpp>"
    )
endif()

if(NOT WIN32 AND NOT APPLE AND NOT UNIX)
    message(FATAL_ERROR "Foundation Utils does not support the current platform.")
endif()

if(NOT TARGET FoundationUtils)
    add_library(FoundationUtils STATIC
        ${FOUNDATION_UTILS_FILES}
        "${FOUNDATION_UTILS_SOURCE_DIR}/FoundationUtils.cpp"
    )
    add_library(Foundation::Utils ALIAS FoundationUtils)
    set_target_properties(FoundationUtils PROPERTIES
        EXPORT_NAME Utils
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../../Bin/$<0:>"
    )

    target_compile_features(FoundationUtils PUBLIC cxx_std_20)
    target_include_directories(FoundationUtils PUBLIC
        "$<BUILD_INTERFACE:${FOUNDATION_UTILS_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:SDK/Include/Foundation/Utils>"
    )

    if(MSVC)
        target_compile_options(FoundationUtils
            PUBLIC /utf-8 /Zc:preprocessor
        )
    endif()

    if(WIN32)
        target_compile_definitions(FoundationUtils PUBLIC WIN32 NOMINMAX)
        target_link_libraries(FoundationUtils PUBLIC dbghelp psapi shlwapi)
    elseif(APPLE)
        find_package(Threads REQUIRED)
        find_package(Iconv REQUIRED)
        target_compile_definitions(FoundationUtils PUBLIC FOUNDATION_MACOS)
        target_link_libraries(FoundationUtils PUBLIC Threads::Threads Iconv::Iconv)
    else()
        find_package(Threads REQUIRED)
        find_package(Iconv REQUIRED)
        target_compile_definitions(FoundationUtils PUBLIC Linux)
        target_link_libraries(FoundationUtils PUBLIC Threads::Threads Iconv::Iconv)
    endif()

    source_group(
        TREE "${FOUNDATION_UTILS_SOURCE_DIR}"
        FILES ${FOUNDATION_UTILS_FILES}
    )
endif()

if(NOT TARGET Foundation::Utils)
    message(FATAL_ERROR "Utils.cmake failed to create the Foundation::Utils target.")
endif()
if(NOT TARGET Foundation::UtilsApi)
    message(FATAL_ERROR "Utils.cmake failed to create the Foundation::UtilsApi target.")
endif()
if(NOT TARGET Foundation::UtilsJson)
    message(FATAL_ERROR "Utils.cmake failed to create the Foundation::UtilsJson target.")
endif()

# Foundation source targets use the archive built in this directory.
link_libraries(Foundation::Utils)


