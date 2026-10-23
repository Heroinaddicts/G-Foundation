include_guard(GLOBAL)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(
    FOUNDATION_ROOT
    "${SOURCE_DIR}"
    ABSOLUTE
)
include("${FOUNDATION_ROOT}/UtilsBuild.cmake")

set(FOUNDATION_THIRD_LIBRARY_CMAKE
    "${FOUNDATION_ROOT}/FoundationThirdLibrary.cmake"
)
if(EXISTS "${FOUNDATION_THIRD_LIBRARY_CMAKE}")
    include("${FOUNDATION_THIRD_LIBRARY_CMAKE}")
    FoundationResolveFoundationThirdParty()
else()
    message(FATAL_ERROR "Foundation third-party manager was not found: ${FOUNDATION_THIRD_LIBRARY_CMAKE}")
endif()

include("${SOURCE_DIR}/FoundationApi.cmake")

file(GLOB_RECURSE COMPONENT_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${SOURCE_DIR}/Components/*"
)
list(SORT COMPONENT_FILES)

set(ROOT_FILES
    "${SOURCE_DIR}/CMakeLists.txt"
    "${SOURCE_DIR}/Foundation.cmake"
    "${SOURCE_DIR}/FoundationApi.cmake"
)

set(FILES
    ${ROOT_FILES}
    ${COMPONENT_FILES}
    ${API_FILES}
)
list(REMOVE_DUPLICATES FILES)
list(SORT FILES)

# Foundation and Ssh are current runtime sources. The public Geometry sources
# also belong to the DLL because their declarations are part of FoundationApi.
set(RUNTIME_SOURCES
    ${COMPONENT_FILES}
)
list(FILTER RUNTIME_SOURCES
    INCLUDE REGEX "\\.(c|cc|cpp|cxx)$"
)
list(FILTER RUNTIME_SOURCES
    EXCLUDE REGEX "/Components/Net/"
)

if(WIN32)
    set(NET_PLATFORM "Windows")
elseif(APPLE)
    set(NET_PLATFORM "MacOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NET_PLATFORM "Linux")
else()
    message(FATAL_ERROR
        "Foundation Net does not support platform '${CMAKE_SYSTEM_NAME}'."
    )
endif()

set(NET_RUNTIME_SOURCES
    "${SOURCE_DIR}/Components/Net/${NET_PLATFORM}/Net.cpp"
    "${SOURCE_DIR}/Components/Net/${NET_PLATFORM}/Tcper.cpp"
    "${SOURCE_DIR}/Components/Net/${NET_PLATFORM}/Accepter.cpp"
)

list(APPEND RUNTIME_SOURCES
    ${API_IMPLEMENTATION_SOURCES}
    ${NET_RUNTIME_SOURCES}
)
list(REMOVE_DUPLICATES RUNTIME_SOURCES)
list(SORT RUNTIME_SOURCES)

# Non-selected and legacy Net translation units remain visible in the project.
# The selected platform sources above are compiled without modifying them.
set(DEFERRED_SOURCES
    ${COMPONENT_FILES}
    ${API_IMPLEMENTATION_SOURCES}
)
list(FILTER DEFERRED_SOURCES
    INCLUDE REGEX "\\.(c|cc|cpp|cxx)$"
)
list(REMOVE_ITEM DEFERRED_SOURCES
    ${RUNTIME_SOURCES}
)

if(DEFERRED_SOURCES)
    set_source_files_properties(
        ${DEFERRED_SOURCES}
        PROPERTIES HEADER_FILE_ONLY TRUE
    )

    list(LENGTH DEFERRED_SOURCES deferredSourceCount)
    message(STATUS
        "Foundation Foundation: ${deferredSourceCount} non-selected or adapted Net sources "
        "are visible in the project and excluded from this platform build."
    )
endif()

source_group(
    TREE "${SOURCE_DIR}"
    FILES ${FILES}
)

function(FoundationRequireFoundationTarget targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "Foundation Foundation target does not exist: ${targetName}")
    endif()
endfunction()

function(FoundationApplyFoundationWarnings targetName)
    FoundationRequireFoundationTarget("${targetName}")

    if(MSVC)
        target_compile_options(${targetName} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:preprocessor
            "$<$<CONFIG:Release>:/Zi>"
            "$<$<CONFIG:MinSizeRel>:/Zi>"
        )
        target_link_options(${targetName} PRIVATE
            "$<$<CONFIG:Release>:/DEBUG:FULL>"
            "$<$<CONFIG:MinSizeRel>:/DEBUG:FULL>"
        )
    else()
        target_compile_options(${targetName} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(FoundationConfigureFoundationTargetOutput targetName outputSubdirectory)
    FoundationRequireFoundationTarget("${targetName}")
    set(outputDirectory "${FOUNDATION_ROOT}/../../Bin")
    if(NOT "${outputSubdirectory}" STREQUAL "")
        string(APPEND outputDirectory "/${outputSubdirectory}")
    endif()
    # Keep Xcode and other multi-config generators from appending a config folder.
    string(APPEND outputDirectory "/$<0:>")
    set_target_properties(${targetName} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${outputDirectory}"
        LIBRARY_OUTPUT_DIRECTORY "${outputDirectory}"
        ARCHIVE_OUTPUT_DIRECTORY "${outputDirectory}"
        PDB_OUTPUT_DIRECTORY "${outputDirectory}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${outputDirectory}"
    )
endfunction()

function(FoundationDeployFoundationRuntime targetName)
    FoundationRequireFoundationTarget("${targetName}")

    if(NOT TARGET Foundation)
        message(FATAL_ERROR "Foundation must exist before deploying its runtime library.")
    endif()
    if("${targetName}" STREQUAL "Foundation")
        message(FATAL_ERROR "Foundation cannot deploy its runtime library to itself.")
    endif()

    add_dependencies(${targetName} Foundation)
    FoundationDeployRuntimeDependencies(${targetName})

    if(MSVC)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PDB_FILE:Foundation>"
                "$<TARGET_FILE_DIR:${targetName}>"
            VERBATIM
        )
    endif()

    if(APPLE)
        set_property(TARGET ${targetName} APPEND PROPERTY BUILD_RPATH "@loader_path")
    elseif(UNIX)
        set_property(TARGET ${targetName} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    endif()

    if(NOT WIN32)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Foundation>"
                "$<TARGET_FILE_DIR:${targetName}>"
            VERBATIM
        )
    endif()
endfunction()

function(FoundationConfigureFoundationRuntimeTarget targetName)
    FoundationRequireFoundationTarget("${targetName}")

    if(NOT TARGET Foundation::FoundationApi)
        message(FATAL_ERROR "Foundation::FoundationApi must exist before configuring the Foundation runtime.")
    endif()

    target_compile_features(${targetName} PUBLIC cxx_std_20)
    target_link_libraries(${targetName}
        PUBLIC Foundation::FoundationApi
        PRIVATE
            ${FOUNDATION_LIBSSH2_TARGET}
            ${FOUNDATION_CURL_TARGET}
            ${FOUNDATION_MARIADB_TARGET}
            ${FOUNDATION_HIREDIS_TARGET}
            ${FOUNDATION_LIBWEBSOCKETS_TARGET}
            ${FOUNDATION_OPENSSL_TARGETS}
            ${FOUNDATION_BOOST_TARGETS}
    )
    target_compile_definitions(${targetName} PRIVATE EXPORTS)

    if(WIN32)
        target_compile_definitions(${targetName} PRIVATE WIN32 NOMINMAX)
        target_link_libraries(${targetName} PRIVATE ws2_32 mswsock)
        if(MSVC)
            set_target_properties(${targetName} PROPERTIES
                PDB_NAME "${targetName}.Runtime"
            )
            target_link_options(${targetName} PRIVATE
                "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${targetName}>"
            )
        endif()
    elseif(APPLE)
        target_compile_definitions(${targetName} PRIVATE FOUNDATION_MACOS)
    else()
        target_compile_definitions(${targetName} PRIVATE Linux)
    endif()
    FoundationDeployRuntimeDependencies(${targetName})
    FoundationApplyFoundationWarnings("${targetName}")
    FoundationConfigureFoundationTargetOutput("${targetName}" "")

    set_target_properties(${targetName} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endfunction()

function(FoundationConfigureFoundationComponentTarget targetName componentName)
    FoundationRequireFoundationTarget("${targetName}")

    if("${componentName}" STREQUAL "")
        message(FATAL_ERROR "A Foundation component name is required for target ${targetName}.")
    endif()
    if(NOT TARGET Foundation::FoundationApi)
        message(FATAL_ERROR "Foundation::FoundationApi must exist before configuring Foundation components.")
    endif()

    target_compile_features(${targetName} PRIVATE cxx_std_20)
    target_link_libraries(${targetName} PRIVATE Foundation::FoundationApi)
    FoundationApplyFoundationWarnings("${targetName}")
    FoundationConfigureFoundationTargetOutput("${targetName}" "Components")
    set_target_properties(${targetName} PROPERTIES FOLDER "Components/${componentName}")
endfunction()

function(FoundationConfigureFoundationTestTarget targetName)
    FoundationRequireFoundationTarget("${targetName}")
    target_compile_features(${targetName} PRIVATE cxx_std_20)
    FoundationApplyFoundationWarnings("${targetName}")
    FoundationConfigureFoundationTargetOutput("${targetName}" "Tests")
endfunction()

add_library(Foundation SHARED
    ${FILES}
)
add_library(Foundation::Foundation ALIAS Foundation)

target_include_directories(Foundation PRIVATE
    "${SOURCE_DIR}/Components"
    "${SOURCE_DIR}/Components/Interface"
)

FoundationConfigureFoundationRuntimeTarget(Foundation)
