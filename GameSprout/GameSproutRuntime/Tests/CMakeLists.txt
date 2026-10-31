cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GameSproutRuntimeChecks LANGUAGES CXX)
    enable_testing()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../GameSproutRuntime.cmake")
add_executable(RuntimeApiChecks RuntimeApiChecks.cpp)
target_link_libraries(RuntimeApiChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
add_test(NAME Runtime.ApiAndCoordinates COMMAND RuntimeApiChecks)

# All fixture outputs stay in the test build tree, never in the product Bin.
foreach(fixture IN ITEMS RuntimeExportGood RuntimeExportNull)
    add_library(${fixture} SHARED RuntimeExportFixture.cpp)
    target_compile_definitions(${fixture} PRIVATE GAMESPROUT_RUNTIME_EXPORTS)
    target_link_libraries(${fixture} PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
endforeach()
target_compile_definitions(RuntimeExportNull PRIVATE GAMESPROUT_TEST_NULL_FACTORY)
add_library(RuntimeExportMissing SHARED RuntimeExportMissingFixture.cpp)
foreach(fixture IN ITEMS RuntimeExportGood RuntimeExportNull RuntimeExportMissing)
    set_target_properties(${fixture} PROPERTIES
        OUTPUT_NAME GameSproutRuntime
        DEBUG_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/加载 测试/${fixture}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/加载 测试/${fixture}/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/加载 测试/${fixture}/$<CONFIG>"
    )
    if(WIN32)
        set_target_properties(${fixture} PROPERTIES PREFIX "")
    endif()
endforeach()
add_executable(RuntimeExportChecks RuntimeExportChecks.cpp RuntimeExportOtherTU.cpp)
target_link_libraries(RuntimeExportChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(MINGW)
    target_link_options(RuntimeExportChecks PRIVATE -municode)
endif()
add_dependencies(RuntimeExportChecks RuntimeExportGood RuntimeExportNull RuntimeExportMissing)
add_test(NAME Runtime.DynamicExport COMMAND RuntimeExportChecks
    "$<TARGET_FILE_DIR:RuntimeExportGood>"
    "$<TARGET_FILE_DIR:RuntimeExportNull>"
    "$<TARGET_FILE_DIR:RuntimeExportMissing>"
)

add_executable(RuntimeImplementationChecks RuntimeImplementationChecks.cpp)
target_link_libraries(RuntimeImplementationChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeImplementationChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.Implementation COMMAND RuntimeImplementationChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")

add_executable(RuntimeRendererChecks RuntimeRendererChecks.cpp)
target_link_libraries(RuntimeRendererChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeRendererChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.RendererGPU COMMAND RuntimeRendererChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.RendererGPU PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

add_executable(RuntimeSkinningChecks RuntimeSkinningChecks.cpp)
target_link_libraries(RuntimeSkinningChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeSkinningChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.SkinningGPU COMMAND RuntimeSkinningChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.SkinningGPU PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

add_executable(RuntimeAnimationChecks RuntimeAnimationChecks.cpp)
target_link_libraries(RuntimeAnimationChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeAnimationChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.Animation COMMAND RuntimeAnimationChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")

add_executable(RuntimeMotionChecks RuntimeMotionChecks.cpp)
target_link_libraries(RuntimeMotionChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeMotionChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.RootMotionAndEvents COMMAND RuntimeMotionChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.RootMotionAndEvents PROPERTIES TIMEOUT 30)

add_executable(RuntimeLayerChecks RuntimeLayerChecks.cpp)
target_link_libraries(RuntimeLayerChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeLayerChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.AnimationLayersAndMasks COMMAND RuntimeLayerChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.AnimationLayersAndMasks PROPERTIES TIMEOUT 30)

add_executable(RuntimeHumanChecks RuntimeHumanChecks.cpp)
target_link_libraries(RuntimeHumanChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeHumanChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.HumanoidIKAndSynchronization COMMAND RuntimeHumanChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.HumanoidIKAndSynchronization PROPERTIES TIMEOUT 30)
