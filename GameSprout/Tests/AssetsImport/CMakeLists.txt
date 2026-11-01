cmake_minimum_required(VERSION 3.21)
project(AssetsImportChecks LANGUAGES CXX)
get_filename_component(gamesprout_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(repository_root "${gamesprout_root}/.." ABSOLUTE)
include("${gamesprout_root}/Foundation/FoundationApi.cmake")
include("${gamesprout_root}/GRHI/GRHI.cmake")
include("${gamesprout_root}/GameSproutRuntime/GameSproutRuntime.cmake")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
add_executable(AssetsImportChecks AssetsImportChecks.cpp)
add_executable(StartupReconcileChecks StartupReconcileChecks.cpp)
add_executable(WarmStartupChecks WarmStartupChecks.cpp)
add_executable(ProjectBrowserChecks ProjectBrowserChecks.cpp)
add_executable(InspectorChecks InspectorChecks.cpp)
add_executable(MaterialInspectorChecks MaterialInspectorChecks.cpp
    "${gamesprout_root}/Module/RuntimeAssets/LibraryRuntimeResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/MeshResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/TextureResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/MaterialResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/AnimationResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/ControllerResources.cpp")
add_executable(ArtifactFormatChecks ArtifactFormatChecks.cpp)
add_executable(CurrentFormatChecks CurrentFormatChecks.cpp)
add_executable(ShaderCompilerChecks ShaderCompilerChecks.cpp)
add_executable(IDEContextMenuChecks IDEContextMenuChecks.cpp)
add_executable(HierarchySceneChecks HierarchySceneChecks.cpp NodeInspectorChecks.h)
add_executable(SceneEditingChecks SceneEditingChecks.cpp)
add_executable(PreparedRenderChecks PreparedRenderChecks.cpp
    "${gamesprout_root}/Module/RuntimeAssets/LibraryRuntimeResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/MeshResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/TextureResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/MaterialResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/AnimationResources.cpp"
    "${gamesprout_root}/Module/RuntimeAssets/ControllerResources.cpp")
foreach(check_target IN ITEMS AssetsImportChecks StartupReconcileChecks WarmStartupChecks ProjectBrowserChecks InspectorChecks MaterialInspectorChecks ArtifactFormatChecks CurrentFormatChecks ShaderCompilerChecks IDEContextMenuChecks HierarchySceneChecks SceneEditingChecks)
target_compile_features(${check_target} PRIVATE cxx_std_20)
target_include_directories(${check_target} PRIVATE
    "${gamesprout_root}/Module/Interface" "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(${check_target} PRIVATE Foundation::FoundationApi GRHI::GRHIApi Qt6::Core Qt6::Gui)
endforeach()
target_compile_features(PreparedRenderChecks PRIVATE cxx_std_20)
target_include_directories(PreparedRenderChecks PRIVATE
    "${gamesprout_root}/Module/Interface" "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(PreparedRenderChecks PRIVATE Foundation::FoundationApi GRHI::GRHIApi
    GameSproutRuntime::GameSproutRuntimeApi Qt6::Core Qt6::Gui)
if(APPLE)
    set(assets_test_binary_root "${repository_root}/Bin/GameSproutEditor.app/Contents/MacOS")
elseif(WIN32)
    set(assets_test_binary_root "${repository_root}/Bin")
else()
    set(assets_test_binary_root "${repository_root}/Bin")
endif()
set(GAMESPROUT_TEST_BINARY_ROOT "${assets_test_binary_root}" CACHE PATH "Independently built editor application binary directory")
set(assets_test_binary_root "${GAMESPROUT_TEST_BINARY_ROOT}")
foreach(check_target IN ITEMS AssetsImportChecks StartupReconcileChecks WarmStartupChecks ProjectBrowserChecks InspectorChecks MaterialInspectorChecks ArtifactFormatChecks CurrentFormatChecks ShaderCompilerChecks IDEContextMenuChecks HierarchySceneChecks SceneEditingChecks)
target_compile_definitions(${check_target} PRIVATE TEST_BINARY_ROOT="${assets_test_binary_root}")
endforeach()
target_compile_definitions(PreparedRenderChecks PRIVATE TEST_BINARY_ROOT="${assets_test_binary_root}")
# Exercise the exported core and the same already-built plugin used by the editor.
# Tests must not add product source subdirectories or overwrite the application DLLs.
add_library(ModelTexturePipeline SHARED IMPORTED)
set(model_texture_module_root "${assets_test_binary_root}/Logic/Module")
set_target_properties(ModelTexturePipeline PROPERTIES
    IMPORTED_LOCATION "${model_texture_module_root}/${CMAKE_SHARED_LIBRARY_PREFIX}AssetsPipeline${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(WIN32)
    set_target_properties(ModelTexturePipeline PROPERTIES
        IMPORTED_IMPLIB "${model_texture_module_root}/AssetsPipeline.lib")
endif()
add_executable(ModelTextureChecks ModelTextureChecks.cpp)
target_compile_features(ModelTextureChecks PRIVATE cxx_std_20)
target_compile_definitions(ModelTextureChecks PRIVATE TEST_BINARY_ROOT="${assets_test_binary_root}")
target_include_directories(ModelTextureChecks PRIVATE
    "${gamesprout_root}/Module/AssetsPipeline" "${gamesprout_root}/Module/Interface"
    "${gamesprout_root}/View/Interface" "${gamesprout_root}/GameSproutEditor/Api")
target_link_libraries(ModelTextureChecks PRIVATE ModelTexturePipeline
    Foundation::FoundationApi GRHI::GRHIApi Qt6::Core Qt6::Gui)
set_target_properties(ModelTextureChecks PROPERTIES BUILD_RPATH "${model_texture_module_root}")
target_link_libraries(ProjectBrowserChecks PRIVATE Qt6::Widgets)
target_link_libraries(InspectorChecks PRIVATE Qt6::Widgets)
target_link_libraries(MaterialInspectorChecks PRIVATE Qt6::Widgets GameSproutRuntime::GameSproutRuntimeApi)
target_link_libraries(IDEContextMenuChecks PRIVATE Qt6::Widgets)
target_link_libraries(HierarchySceneChecks PRIVATE Qt6::Widgets GameSproutRuntime::GameSproutRuntimeApi)
set_property(TARGET HierarchySceneChecks APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>")
add_custom_command(TARGET HierarchySceneChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
        "$<TARGET_FILE_DIR:HierarchySceneChecks>/$<TARGET_FILE_NAME:GameSproutRuntime::GameSproutRuntime>"
    VERBATIM
)
target_link_libraries(SceneEditingChecks PRIVATE Qt6::Widgets GameSproutRuntime::GameSproutRuntimeApi)
set_property(TARGET SceneEditingChecks APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>")
add_custom_command(TARGET SceneEditingChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:GameSproutRuntime::GameSproutRuntime>"
        "$<TARGET_FILE_DIR:SceneEditingChecks>/$<TARGET_FILE_NAME:GameSproutRuntime::GameSproutRuntime>"
    VERBATIM
)
enable_testing()
add_test(NAME Assets.ModelTextures COMMAND ModelTextureChecks)
set_tests_properties(Assets.ModelTextures PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
add_test(NAME IDE.PreparedRendering COMMAND PreparedRenderChecks)
add_test(NAME Assets.PreparedRenderData COMMAND PreparedRenderChecks --provider-only)
set_tests_properties(IDE.PreparedRendering PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
set_tests_properties(Assets.PreparedRenderData PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME Assets.ShaderCompiler COMMAND ShaderCompilerChecks)
add_test(NAME Assets.ArtifactFormat COMMAND ArtifactFormatChecks)
add_test(NAME Assets.CurrentFormat COMMAND CurrentFormatChecks)
set_tests_properties(Assets.CurrentFormat PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME Assets.ImportLifecycle COMMAND AssetsImportChecks)
add_test(NAME Assets.StartupReconcile COMMAND StartupReconcileChecks)
add_test(NAME Assets.WarmStartup COMMAND WarmStartupChecks)
set_tests_properties(Assets.WarmStartup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
add_test(NAME Assets.ProjectBrowser COMMAND ProjectBrowserChecks)
add_test(NAME Assets.ProjectBrowserLate COMMAND ProjectBrowserChecks --late)
add_test(NAME Assets.Inspector COMMAND InspectorChecks)
add_test(NAME Assets.MaterialInspector COMMAND MaterialInspectorChecks)
set_tests_properties(Assets.MaterialInspector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME IDE.ContextMenu COMMAND IDEContextMenuChecks)
add_test(NAME IDE.HierarchyScene COMMAND HierarchySceneChecks)
set_tests_properties(IDE.HierarchyScene PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
set_tests_properties(IDE.ContextMenu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
set_tests_properties(Assets.Inspector PROPERTIES TIMEOUT 90)
set_tests_properties(Assets.ImportLifecycle Assets.StartupReconcile Assets.ProjectBrowser Assets.ProjectBrowserLate Assets.Inspector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME IDE.SceneEditing COMMAND SceneEditingChecks)
set_tests_properties(IDE.SceneEditing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
