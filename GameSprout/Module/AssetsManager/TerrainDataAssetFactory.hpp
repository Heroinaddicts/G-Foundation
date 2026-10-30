#pragma once

#include "AssetFactoryCommon.hpp"

class TerrainDataImportSettings : public NativeAssetImportSettings<ITerrainDataImportSettings> {
public:
    virtual ~TerrainDataImportSettings() {}
};

class TerrainDataMeta : public AssetMetaWithSettings<TerrainDataImportSettings> {
public:
    TerrainDataMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::TerrainData, guid, path) {}
};

inline IAssetMeta * OnTerrainDataAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<TerrainDataMeta>(event);
}
