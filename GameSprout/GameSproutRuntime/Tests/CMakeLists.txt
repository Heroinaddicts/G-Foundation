cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GameSproutRuntimeChecks LANGUAGES CXX)
    enable_testing()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../GameSproutRuntime.cmake")
add_executable(RuntimeApiChecks RuntimeApiChecks.cpp)
target_link_libraries(RuntimeApiChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
add_test(NAME Runtime.ApiAndCoordinates COMMAND RuntimeApiChecks)

foreach(physics_check RuntimePhysicsChecks RuntimeComponentIdentityChecks)
    add_executable(${physics_check} ${physics_check}.cpp)
    target_link_libraries(${physics_check} PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
    if(TARGET GameSproutRuntime)
        add_dependencies(${physics_check} GameSproutRuntime)
    endif()
endforeach()
add_test(NAME Runtime.Physics COMMAND RuntimePhysicsChecks "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
add_test(NAME Runtime.ComponentIdentities COMMAND RuntimeComponentIdentityChecks "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.Physics Runtime.ComponentIdentities PROPERTIES TIMEOUT 60)

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

add_executable(RuntimeClipPlaybackChecks RuntimeClipPlaybackChecks.cpp)
target_link_libraries(RuntimeClipPlaybackChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeClipPlaybackChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.DirectClipPlayback COMMAND RuntimeClipPlaybackChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.DirectClipPlayback PROPERTIES TIMEOUT 30)

# A shell can consume every rendering/resource entry without GPU headers.
add_executable(RuntimeSdkBoundaryChecks RuntimeSdkBoundaryChecks.cpp)
target_link_libraries(RuntimeSdkBoundaryChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
add_test(NAME Runtime.PublicSdkBoundary COMMAND RuntimeSdkBoundaryChecks)

add_library(NativeScriptFixture SHARED NativeScriptFixture.cpp)
target_link_libraries(NativeScriptFixture PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
set_target_properties(NativeScriptFixture PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
add_executable(RuntimeScriptChecks RuntimeScriptChecks.cpp)
target_link_libraries(RuntimeScriptChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
add_dependencies(RuntimeScriptChecks NativeScriptFixture)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeScriptChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.NativeScripts COMMAND RuntimeScriptChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>" "$<TARGET_FILE:NativeScriptFixture>")

add_executable(RuntimeExecutionChecks RuntimeExecutionChecks.cpp)
target_link_libraries(RuntimeExecutionChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeExecutionChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.ExecutionSession COMMAND RuntimeExecutionChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")

add_executable(RuntimeInputChecks RuntimeInputChecks.cpp)
target_link_libraries(RuntimeInputChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeInputChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.GameplayInput COMMAND RuntimeInputChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")

add_executable(RuntimeObjectCreationChecks RuntimeObjectCreationChecks.cpp)
target_link_libraries(RuntimeObjectCreationChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeObjectCreationChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.ObjectCreation COMMAND RuntimeObjectCreationChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.ObjectCreation PROPERTIES TIMEOUT 30)

add_executable(RuntimeInitializationChecks RuntimeInitializationChecks.cpp)
target_link_libraries(RuntimeInitializationChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeInitializationChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.ComponentInitialization COMMAND RuntimeInitializationChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.ComponentInitialization PROPERTIES TIMEOUT 30)

add_executable(RuntimeDetachedObjectChecks RuntimeDetachedObjectChecks.cpp)
target_link_libraries(RuntimeDetachedObjectChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeDetachedObjectChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.DetachedObjects COMMAND RuntimeDetachedObjectChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.DetachedObjects PROPERTIES TIMEOUT 30)

add_executable(RuntimeScriptObjectReferenceChecks RuntimeScriptObjectReferenceChecks.cpp)
target_link_libraries(RuntimeScriptObjectReferenceChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeScriptObjectReferenceChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.ScriptObjectReferences COMMAND RuntimeScriptObjectReferenceChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.ScriptObjectReferences PROPERTIES TIMEOUT 30)

if(NOT TARGET ObjectDocument::ObjectDocument)
    include("${CMAKE_CURRENT_LIST_DIR}/../../ObjectDocument/ObjectDocument.cmake")
endif()
add_executable(RuntimePrefabChecks RuntimePrefabChecks.cpp)
target_link_libraries(RuntimePrefabChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi ObjectDocument::ObjectDocument)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimePrefabChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.PrefabAssembly COMMAND RuntimePrefabChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.PrefabAssembly PROPERTIES TIMEOUT 30)

add_executable(RuntimeTagsAndLayersChecks RuntimeTagsAndLayersChecks.cpp)
target_link_libraries(RuntimeTagsAndLayersChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeTagsAndLayersChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.TagsLayersAndCollisionMatrix COMMAND RuntimeTagsAndLayersChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.TagsLayersAndCollisionMatrix PROPERTIES TIMEOUT 60)

add_executable(RuntimeStaticFlagsChecks RuntimeStaticFlagsChecks.cpp)
target_link_libraries(RuntimeStaticFlagsChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeStaticFlagsChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.StaticDeclarations COMMAND RuntimeStaticFlagsChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.StaticDeclarations PROPERTIES TIMEOUT 60)

add_executable(RuntimeLivePropertiesChecks RuntimeLivePropertiesChecks.cpp)
target_link_libraries(RuntimeLivePropertiesChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeLivePropertiesChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.LiveProperties COMMAND RuntimeLivePropertiesChecks "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
set_tests_properties(Runtime.LiveProperties PROPERTIES TIMEOUT 60)

add_executable(RuntimeRecursiveScriptChecks RuntimeRecursiveScriptChecks.cpp)
target_link_libraries(RuntimeRecursiveScriptChecks PRIVATE GameSproutRuntime::GameSproutRuntimeApi)
if(TARGET GameSproutRuntime)
    add_dependencies(RuntimeRecursiveScriptChecks GameSproutRuntime)
endif()
add_test(NAME Runtime.RecursiveScriptFields COMMAND RuntimeRecursiveScriptChecks
    "$<TARGET_FILE_DIR:GameSproutRuntime::GameSproutRuntime>")
