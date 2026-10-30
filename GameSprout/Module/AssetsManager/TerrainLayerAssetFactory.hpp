#pragma once

#include "AssetFactoryCommon.hpp"

class TerrainLayerImportSettings : public NativeAssetImportSettings<ITerrainLayerImportSettings> {
public:
    virtual ~TerrainLayerImportSettings() {}
};

class TerrainLayerMeta : public AssetMetaWithSettings<TerrainLayerImportSettings> {
public:
    TerrainLayerMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::TerrainLayer, guid, path) {}
};

inline IAssetMeta * OnTerrainLayerAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<TerrainLayerMeta>(event);
}
