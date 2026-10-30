#include "AssetsManager.h"
#include "TextureAssetFactory.hpp"
#include "AudioAssetFactory.hpp"
#include "MeshAssetFactory.hpp"
#include "ModelAssetFactory.hpp"
#include "MaterialAssetFactory.hpp"
#include "ShaderAssetFactory.hpp"
#include "ComputeShaderAssetFactory.hpp"
#include "ShaderIncludeAssetFactory.hpp"
#include "FontAssetFactory.hpp"
#include "VideoAssetFactory.hpp"
#include "TextAssetFactory.hpp"
#include "ScriptAssetFactory.hpp"
#include "SceneAssetFactory.hpp"
#include "AnimationClipAssetFactory.hpp"
#include "AnimatorControllerAssetFactory.hpp"
#include "AnimatorOverrideControllerAssetFactory.hpp"
#include "AvatarMaskAssetFactory.hpp"
#include "AudioMixerAssetFactory.hpp"
#include "PhysicsMaterialAssetFactory.hpp"
#include "PhysicsMaterial2DAssetFactory.hpp"
#include "RenderTextureAssetFactory.hpp"
#include "CubemapAssetFactory.hpp"
#include "SpriteAtlasAssetFactory.hpp"
#include "TerrainDataAssetFactory.hpp"
#include "TerrainLayerAssetFactory.hpp"
#include "ShaderVariantCollectionAssetFactory.hpp"
#include "DataAssetAssetFactory.hpp"
#include "LightingSettingsAssetFactory.hpp"
#include "LightmapParametersAssetFactory.hpp"
#include "FolderAssetFactory.hpp"

bool AssetsManager::Initialize(GameSproutApi * const api) {
    g_GameSproutApi = api;
    GetGameSproutModule(api->_EditorApi, g_IProject, Project);
    GetGameSproutModule(api->_EditorApi, g_IConsole, Console);
    GetGameSproutView(api->_EditorApi, g_IIDEWindow, IDEWindow);
    GetGameSproutModule(api->_EditorApi, g_IGuidCreator, GuidCreator);
    s_Active = this;
    GetGameSproutModule(api->_EditorApi, _Pipeline, AssetsPipeline);
    GetGameSproutModule(api->_EditorApi, _Library, LibraryManager);

    GSRegLaunchedCallback(
        g_IProject,
        [&](GameSproutApi * const api) -> bool {
            if (api->_Foundation->GetLaunchArg("IsNewProject")) {
                QDir dir(QString::fromStdString(g_IProject->GetProjectRoot()));
                this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets").toStdString();
                GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "New Project Create Assets Dir {}", this->_AssetsRoot);
                return dir.mkdir("Assets");
            }

            this->_AssetsRoot = QString::fromStdString(g_IProject->GetProjectRoot() + "/Assets").toStdString();
            GameSproutConsoleLog(g_IConsole, eConsoleLog::Trace, "Project Assets Dir {}", this->_AssetsRoot);
            QDir dir(QString::fromStdString(this->_AssetsRoot));
            return dir.exists();
        }
    );

    AssetsManager::RegistFactory(eAssetImporterType::Texture, OnTextureAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Audio, OnAudioAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Mesh, OnMeshAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Model, OnModelAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Material, OnMaterialAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Shader, OnShaderAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::ComputeShader, OnComputeShaderAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::ShaderInclude, OnShaderIncludeAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Font, OnFontAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Video, OnVideoAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Text, OnTextAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Script, OnScriptAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Scene, OnSceneAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::AnimationClip, OnAnimationClipAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::AnimatorController, OnAnimatorControllerAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::AnimatorOverrideController, OnAnimatorOverrideControllerAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::AvatarMask, OnAvatarMaskAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::AudioMixer, OnAudioMixerAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::PhysicsMaterial, OnPhysicsMaterialAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::PhysicsMaterial2D, OnPhysicsMaterial2DAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::RenderTexture, OnRenderTextureAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Cubemap, OnCubemapAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::SpriteAtlas, OnSpriteAtlasAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::TerrainData, OnTerrainDataAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::TerrainLayer, OnTerrainLayerAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::ShaderVariantCollection, OnShaderVariantCollectionAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::DataAsset, OnDataAssetAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::LightingSettings, OnLightingSettingsAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::LightmapParameters, OnLightmapParametersAssetCreate);
    AssetsManager::RegistFactory(eAssetImporterType::Folder, OnFolderAssetCreate);
    return true;
}

bool AssetsManager::Launch(GameSproutApi * const api) {
    if (!_Pipeline || !_Library || !g_IIDEWindow) return false;
    // Keep the editor blocked across worker completions and main-thread batches.
    g_IIDEWindow->Loading("初始化资源", "扫描 Assets 并校验资源数据库");
    _StartupLoading = true;
    return true;
}


void AssetsManager::LaunchFinished(GameSproutApi * const api) {
    _ReconcileOnScan = true;
    _ScanFailed = false;
    _StartupWatchesVerified = false;
    _ConflictingGuids.clear();
    if (!OpenSourceDatabase()) {
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot read asset source database; startup comparison stopped");
        FinishStartupLoading();
        return;
    }
    _CanonicalAssetsRoot = QDir(QString::fromStdString(_AssetsRoot)).canonicalPath().toStdString();
    _StartupSourceRecords = _SourceRecords;
    CheckDatabaseSources();
    // The Assets directory itself is observed through its parent, per watcher semantics.
    WatcherDir(g_IProject->GetProjectRoot());
    WatcherDir(_AssetsRoot);
    ScanDirectory(_AssetsRoot);
}

void AssetsManager::StoreAssetMeta(IAssetMeta * const meta) {
    if (nullptr == meta) return;
    const std::string guid = meta->_Guid.GetGuidString();
    const auto existingGuid = _Assets.find(guid);
    if (existingGuid != _Assets.end()) {
        if (existingGuid->second->GetPath() != meta->GetPath()) {
            _ConflictingGuids.insert(guid);
            _Pipeline->Invalidate(meta->_Guid);
            _Dirty.erase(guid);
            const auto previous = _SourceRecords.find(guid);
            if (previous != _SourceRecords.end()) RecordSourceError(previous->second.Path, "Duplicate asset GUID");
            GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Duplicate asset GUID {}: {} and {}",guid,existingGuid->second->GetPath(),meta->GetPath());
        }
        delete meta; // 重复事件不替换已经被上层持有的 Meta 对象。
        return;
    }
    if (_AssetGuidsByPath.find(meta->GetPath()) != _AssetGuidsByPath.end()) {
        qWarning() << "Asset path already has a different GUID:" << QString::fromStdString(meta->GetPath());
        delete meta;
        return;
    }
    _AssetGuidsByPath.emplace(meta->GetPath(), guid);
    _Assets.emplace(guid, meta);
    QueueImport(meta);
}

void AssetsManager::Release(GameSproutApi * const api) {
    s_Active = nullptr;
    _AssetEvents.clear(); _AssetListeners.clear();
    FinishStartupLoading();
    for (const auto & entry : _Assets) delete entry.second;
    _Assets.clear(); _AssetGuidsByPath.clear(); _Dirty.clear();
    _DirectoriesToScan.clear();
    _Watches.clear(); _Importing.clear(); _ForceImports.clear(); _ConflictingGuids.clear();
    _BackupSourcesByPath.clear();
    _SourceRecords.clear(); _SourceGuidsByPath.clear(); _StartupSourceRecords.clear(); _SourceDatabaseReady = false;
    s_AssetFactorys.clear();
}

IAssetMeta * AssetsManager::QueryAsset(const Guid& guid) const {
    const auto it = _Assets.find(guid.GetGuidString());
    return it != _Assets.end() ? it->second : nullptr;
}

void AssetsManager::FinishStartupLoading() {
    if (!_StartupLoading) return;
    _StartupLoading = false;
    g_IIDEWindow->Unloading();
}
