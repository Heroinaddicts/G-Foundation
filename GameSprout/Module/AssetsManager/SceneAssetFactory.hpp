#pragma once

#include "AssetFactoryCommon.hpp"

class SceneImportSettings : public NativeAssetImportSettings<ISceneImportSettings> {
public:
    virtual ~SceneImportSettings() {}
};

class SceneMeta : public AssetMetaWithSettings<SceneImportSettings> {
public:
    SceneMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::Scene, guid, path) {}
};

inline IAssetMeta * OnSceneAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<SceneMeta>(event);
}
