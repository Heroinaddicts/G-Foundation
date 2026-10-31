cmake_minimum_required(VERSION 3.21)
project(SceneDocumentChecks LANGUAGES CXX)

get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
include("${gamesprout_root}/GRHI/GRHI.cmake")
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Consume the prebuilt Runtime SDK. This test must not compile Runtime sources.
if(TARGET GameSproutRuntime)
    message(FATAL_ERROR "SceneDocumentChecks must consume the independently built Runtime library")
endif()
add_executable(SceneDocumentChecks
    SceneDocumentChecks.cpp
    "${gamesprout_root}/Module/SceneManager/SceneDocument.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneSerialization.cpp"
)
target_include_directories(SceneDocumentChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager"
)
target_link_libraries(SceneDocumentChecks PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntime
)
add_custom_command(TARGET SceneDocumentChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
        "$<TARGET_FILE_DIR:SceneDocumentChecks>/$<TARGET_FILE_NAME:GameSproutRuntime::GameSproutRuntime>"
    VERBATIM
)
source_group(TREE "${gamesprout_root}" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/SceneDocumentChecks.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDocument.cpp"
)
enable_testing()
add_test(NAME SceneDocument.Instantiation COMMAND SceneDocumentChecks)
set_tests_properties(SceneDocument.Instantiation PROPERTIES TIMEOUT 30)

add_executable(SceneManagerChecks
    SceneManagerChecks.cpp
    "${gamesprout_root}/Module/SceneManager/SceneManager.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneAssetFiles.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDeletion.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneTransformEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDocument.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneSerialization.cpp"
)
target_include_directories(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api"
)
target_link_libraries(SceneManagerChecks PRIVATE
    Foundation::FoundationApi
    GRHI::GRHIApi
    GameSproutRuntime::GameSproutRuntime
    Qt${QT_VERSION_MAJOR}::Core
)
add_custom_command(TARGET SceneManagerChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
        "$<TARGET_FILE_DIR:SceneManagerChecks>/$<TARGET_FILE_NAME:GameSproutRuntime::GameSproutRuntime>"
    VERBATIM
)
add_test(NAME SceneDocument.EditingAndPersistence COMMAND SceneManagerChecks)
set_tests_properties(SceneDocument.EditingAndPersistence PROPERTIES TIMEOUT 30)

# Animation source serialization is an editor Module implementation, never a View dependency.
target_sources(SceneDocumentChecks PRIVATE "${gamesprout_root}/Module/SceneManager/SceneAnimationDocument.cpp")
target_sources(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager/SceneAnimationDocument.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneModelEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneAnimationPreview.cpp")
