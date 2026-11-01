include_guard(GLOBAL)

# Public SDK only: expose Api/ and an independently built library. Consumers
# must not add the Runtime implementation as a subdirectory to access its API.
if(NOT TARGET GameSproutRuntime::GameSproutRuntimeApi)
    add_library(GameSproutRuntimeApi INTERFACE)
    add_library(GameSproutRuntime::GameSproutRuntimeApi ALIAS GameSproutRuntimeApi)
    target_compile_features(GameSproutRuntimeApi INTERFACE cxx_std_20)
    target_include_directories(GameSproutRuntimeApi INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/Api"
    )
    # The header-only Export helper uses the platform dynamic loader.
    target_link_libraries(GameSproutRuntimeApi INTERFACE ${CMAKE_DL_LIBS})
endif()

if(NOT TARGET GameSproutRuntime::GameSproutRuntime)
    add_library(GameSproutRuntime::GameSproutRuntime SHARED IMPORTED GLOBAL)
    get_filename_component(gamesprout_runtime_binary_directory
        "${CMAKE_CURRENT_LIST_DIR}/../../Bin" ABSOLUTE)
    if(WIN32)
        set(gamesprout_runtime_binary_name "GameSproutRuntime${CMAKE_SHARED_LIBRARY_SUFFIX}")
    else()
        set(gamesprout_runtime_binary_name
            "${CMAKE_SHARED_LIBRARY_PREFIX}GameSproutRuntime${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    set_target_properties(GameSproutRuntime::GameSproutRuntime PROPERTIES
        IMPORTED_LOCATION "${gamesprout_runtime_binary_directory}/${gamesprout_runtime_binary_name}"
        INTERFACE_LINK_LIBRARIES GameSproutRuntime::GameSproutRuntimeApi
    )
    if(WIN32)
        set_target_properties(GameSproutRuntime::GameSproutRuntime PROPERTIES
            IMPORTED_IMPLIB
                "${gamesprout_runtime_binary_directory}/${CMAKE_IMPORT_LIBRARY_PREFIX}GameSproutRuntime${CMAKE_IMPORT_LIBRARY_SUFFIX}"
        )
    endif()
endif()

# Packaging is a Runtime SDK responsibility. It does not expose GPU headers or
# backend objects to consumers of GameSproutRuntimeApi.
function(GameSproutRuntimeDeploy target)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../GRHI/GRHI.cmake")
    get_target_property(backend_dependencies GRHI::GRHI GRHI_RUNTIME_DEPENDENCIES)
    add_custom_target(${target}DeployRuntime
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:GRHI::GRHI>" ${backend_dependencies} "$<TARGET_FILE_DIR:${target}>"
        DEPENDS "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
            "$<TARGET_FILE:GRHI::GRHI>" ${backend_dependencies}
        COMMENT "Deploying Runtime SDK and its private graphics dependencies"
        VERBATIM)
    add_dependencies(${target} ${target}DeployRuntime)
endfunction()
