cmake_minimum_required(VERSION 3.21)
project(SceneDocumentChecks LANGUAGES CXX)

get_filename_component(gamesprout_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
include("${gamesprout_root}/ObjectDocument/ObjectDocument.cmake")
set(GAMESPROUT_TEST_RUNTIME_LIBRARY "" CACHE FILEPATH "Independent Runtime library for verification")
if(GAMESPROUT_TEST_RUNTIME_LIBRARY)
    set_target_properties(GameSproutRuntime::GameSproutRuntime PROPERTIES IMPORTED_LOCATION "${GAMESPROUT_TEST_RUNTIME_LIBRARY}")
endif()
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

# Consume the prebuilt Runtime SDK. This test must not compile Runtime sources.
if(TARGET GameSproutRuntime)
    message(FATAL_ERROR "SceneDocumentChecks must consume the independently built Runtime library")
endif()
add_executable(SceneDocumentChecks
    SceneDocumentChecks.cpp
)
target_include_directories(SceneDocumentChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager"
)
target_link_libraries(SceneDocumentChecks PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntime
    ObjectDocument::ObjectDocument
)
add_custom_command(TARGET SceneDocumentChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
        "$<TARGET_FILE_DIR:SceneDocumentChecks>/$<TARGET_FILE_NAME:GameSproutRuntime::GameSproutRuntime>"
    VERBATIM
)
source_group(TREE "${gamesprout_root}" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/SceneDocumentChecks.cpp"
)
enable_testing()
add_test(NAME SceneDocument.Instantiation COMMAND SceneDocumentChecks)
set_tests_properties(SceneDocument.Instantiation PROPERTIES TIMEOUT 30)

add_executable(SceneManagerChecks
    SceneManagerChecks.cpp
    "${gamesprout_root}/Module/SceneManager/SceneManager.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneAssetFiles.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDocumentEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDocumentSession.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneDeletion.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneTransformEditing.cpp"
)
target_include_directories(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager"
    "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/GameSproutEditor/Api"
)
target_link_libraries(SceneManagerChecks PRIVATE
    Foundation::FoundationApi
    GameSproutRuntime::GameSproutRuntime
    ObjectDocument::ObjectDocument
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

# Scene business fixtures consume the same standalone object-document codec.

target_sources(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager/SceneComponents.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneClassification.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneStaticFlags.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneComponentIdentity.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneModelEditing.cpp"
    "${gamesprout_root}/Module/SceneManager/SceneSession.cpp")
target_sources(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/SceneManager/ScenePlaySession.cpp"
    "${gamesprout_root}/Module/SelectionManager/SelectionManager.cpp")

# The host and preview are independent production modules in the registry fixture.
target_sources(SceneManagerChecks PRIVATE
    "${gamesprout_root}/Module/RuntimeHost/RuntimeHost.cpp"
    "${gamesprout_root}/Module/AnimationPreview/AnimationPreview.cpp")
