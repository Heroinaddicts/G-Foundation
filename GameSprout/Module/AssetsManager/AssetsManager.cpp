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

    AssetsManager::RegistFactory(eAssetType::Texture, OnTextureAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Audio, OnAudioAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Mesh, OnMeshAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Model, OnModelAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Material, OnMaterialAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Shader, OnShaderAssetCreate);
    AssetsManager::RegistFactory(eAssetType::ComputeShader, OnComputeShaderAssetCreate);
    AssetsManager::RegistFactory(eAssetType::ShaderInclude, OnShaderIncludeAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Font, OnFontAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Video, OnVideoAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Text, OnTextAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Script, OnScriptAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Scene, OnSceneAssetCreate);
    AssetsManager::RegistFactory(eAssetType::AnimationClip, OnAnimationClipAssetCreate);
    AssetsManager::RegistFactory(eAssetType::AnimatorController, OnAnimatorControllerAssetCreate);
    AssetsManager::RegistFactory(eAssetType::AnimatorOverrideController, OnAnimatorOverrideControllerAssetCreate);
    AssetsManager::RegistFactory(eAssetType::AvatarMask, OnAvatarMaskAssetCreate);
    AssetsManager::RegistFactory(eAssetType::AudioMixer, OnAudioMixerAssetCreate);
    AssetsManager::RegistFactory(eAssetType::PhysicsMaterial, OnPhysicsMaterialAssetCreate);
    AssetsManager::RegistFactory(eAssetType::PhysicsMaterial2D, OnPhysicsMaterial2DAssetCreate);
    AssetsManager::RegistFactory(eAssetType::RenderTexture, OnRenderTextureAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Cubemap, OnCubemapAssetCreate);
    AssetsManager::RegistFactory(eAssetType::SpriteAtlas, OnSpriteAtlasAssetCreate);
    AssetsManager::RegistFactory(eAssetType::TerrainData, OnTerrainDataAssetCreate);
    AssetsManager::RegistFactory(eAssetType::TerrainLayer, OnTerrainLayerAssetCreate);
    AssetsManager::RegistFactory(eAssetType::ShaderVariantCollection, OnShaderVariantCollectionAssetCreate);
    AssetsManager::RegistFactory(eAssetType::DataAsset, OnDataAssetAssetCreate);
    AssetsManager::RegistFactory(eAssetType::LightingSettings, OnLightingSettingsAssetCreate);
    AssetsManager::RegistFactory(eAssetType::LightmapParameters, OnLightmapParametersAssetCreate);
    AssetsManager::RegistFactory(eAssetType::Folder, OnFolderAssetCreate);
    return true;
}

bool AssetsManager::Launch(GameSproutApi * const api)  {
    return true;
}


void AssetsManager::WatcherDir(const std::string & path) {
    g_GameSproutApi->_Foundation->SystemApi()->FileSystemWatcher(
        path,
        s_Active,
        AssetsManager::OnFileSystemWatcher
    );

    std::vector<FoundationUtils::SafeSystem::File::FileInfo> files;
    int count = FoundationUtils::SafeSystem::File::EnumerateDirectory(path, "*", files, false);
    for(int i=0; i<count; i++) {
        if (files[i]._IsDirectory) {
            WatcherDir(files[i]._ParentDirectory + "/" + files[i]._Name);
        }
    }
}

void AssetsManager::LaunchFinished(GameSproutApi * const api)  {
    WatcherDir(_AssetsRoot);
    ScanDirectory(_AssetsRoot);
}

void AssetsManager::ScanDirectory(const std::string & path) {
    std::vector<FoundationUtils::SafeSystem::File::FileInfo> infos;
    int count = FoundationUtils::SafeSystem::File::EnumerateDirectory(path, "*", infos, true, {}, {"meta"});
    for(int i=0; i<count; i++) {
        const auto & info = infos[i];
        const std::string filename = info._Name + (info._Externsion.empty() ? "" : "." + info._Externsion);
        Foundation::FileSystemWatcherEvent event;
        event.Type = info._IsDirectory ? Foundation::eWatcherType::Directory : Foundation::eWatcherType::File;
        event.Action = Foundation::eFileSystemAction::Create;
        event.Path = QDir(QString::fromStdString(info._ParentDirectory)).filePath(
            QString::fromStdString(filename)).toStdString();
        if (info._IsDirectory) {
            StoreAssetMeta(TriggerFactory(eAssetType::Folder, event, info._Name, "", this));
        } else {
            OnFileCreate(event, info._Name, info._Externsion, this);
        }
    }
}

void AssetsManager::StoreAssetMeta(IAssetMeta * const meta) {
    if (nullptr == meta) return;
    const std::string guid = meta->_Guid.GetGuidString();
    const auto existingGuid = _Assets.find(guid);
    if (existingGuid != _Assets.end()) {
        if (existingGuid->second->GetPath() != meta->GetPath()) {
            qWarning() << "Duplicate asset GUID:" << QString::fromStdString(guid)
                       << QString::fromStdString(meta->GetPath());
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
}

void AssetsManager::RemoveAssetRecords(const std::string & path, const bool recursive) {
    const QString root = QDir(QString::fromStdString(g_IProject->GetProjectRoot())).filePath(QStringLiteral("Assets"));
    QString assetsRoot = QDir(root).canonicalPath();
    if (assetsRoot.isEmpty()) assetsRoot = QDir::cleanPath(root);
    const std::string relativePath = QDir(assetsRoot).relativeFilePath(QString::fromStdString(path)).toStdString();
    const std::string prefix = relativePath + "/";
    for (auto it = _AssetGuidsByPath.begin(); it != _AssetGuidsByPath.end();) {
        if (it->first == relativePath || (recursive && it->first.compare(0, prefix.size(), prefix) == 0)) {
            const auto asset = _Assets.find(it->second);
            if (asset != _Assets.end()) {
                delete asset->second;
                _Assets.erase(asset);
            }
            it = _AssetGuidsByPath.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetsManager::Update(GameSproutApi * const api)  {
}

void AssetsManager::Release(GameSproutApi * const api)  {
    s_Active = nullptr;
    for (const auto & entry : _Assets) delete entry.second;
    _Assets.clear();
    _AssetGuidsByPath.clear();
}


IAssetMeta * AssetsManager::QueryAsset(const Guid& guid) const {
    const auto it = _Assets.find(guid.GetGuidString());
    return it != _Assets.end() ? it->second : nullptr;
}


void AssetsManager::OnFileCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
    if (META_EXTENSION == ToLower(extension)) {
        return;
    }

    const eAssetType type = GetAssetType(extension);
    AssetsManager * owner = static_cast<AssetsManager *>(context);
    if (nullptr == owner || owner != s_Active) return;
    owner->StoreAssetMeta(TriggerFactory(type, event, name, extension, context));
}

void AssetsManager::OnFileDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
    if (META_EXTENSION == ToLower(extension)) {

    } else {
        QFile::remove(QString::fromStdString(event.Path + "." + META_EXTENSION));
        AssetsManager * owner = static_cast<AssetsManager *>(context);
        if (nullptr != owner && owner == s_Active) owner->RemoveAssetRecords(event.Path, false);
    }
}

void AssetsManager::OnFileModify(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
}

void AssetsManager::OnFileRename(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
}

void AssetsManager::OnFolderCreate(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
    AssetsManager * owner = static_cast<AssetsManager *>(context);
    if (nullptr == owner || owner != s_Active) return;
    owner->StoreAssetMeta(TriggerFactory(eAssetType::Folder, event, name, "", context));
    // 移入的目录可能已经带有文件；不能只等待后续的子文件创建事件。
    owner->ScanDirectory(event.Path);
}

void AssetsManager::OnFolderDelete(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
    QFile::remove(QString::fromStdString(event.Path + "." + META_EXTENSION));
    AssetsManager * owner = static_cast<AssetsManager *>(context);
    if (nullptr != owner && owner == s_Active) owner->RemoveAssetRecords(event.Path, true);
}

void AssetsManager::OnFolderModify(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
}

void AssetsManager::OnFolderRename(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
}


void AssetsManager::OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event, const std::string & name, const std::string & extension, void * const context) {
    if (nullptr == s_Active || context != s_Active) return;
    qInfo() << event.OldPath << event.Path << (int)event.Type << (int)event.Action;

    switch(event.Action) {
    case eFileSystemAction::Create:
        switch(event.Type) {
        case Foundation::eWatcherType::Directory: {
            OnFolderCreate(event, name, extension, context);
            WatcherDir(event.Path);
            break;
        }
        case Foundation::eWatcherType::File: {
            OnFileCreate(event, name, extension, context);
            break;
        }
        }
        break;
    case Foundation::eFileSystemAction::Modify: {
        switch(event.Type) {
        case Foundation::eWatcherType::Directory: {
            OnFolderModify(event, name, extension, context);
            break;
        }
        case Foundation::eWatcherType::File: {
            OnFileModify(event, name, extension, context);
            break;
        }
        }
        break;
    }
    case Foundation::eFileSystemAction::Delete: {
        switch(event.Type) {
        case Foundation::eWatcherType::Directory: {
            OnFolderDelete(event, name, extension, context);
            break;
        }
        case Foundation::eWatcherType::File: {
            OnFileDelete(event, name, extension, context);
            break;
        }
        }
        break;
    }
    case Foundation::eFileSystemAction::Rename: {
        switch(event.Type) {
        case Foundation::eWatcherType::Directory: {
            OnFolderRename(event, name, extension, context);
            break;
        }
        case Foundation::eWatcherType::File: {
            OnFileRename(event, name, extension, context);
            break;
        }
        }
        break;
    }
    default:
        break;
    }
}
