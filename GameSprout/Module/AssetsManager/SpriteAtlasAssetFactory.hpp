#pragma once

#include "AssetFactoryCommon.hpp"

class SpriteAtlasImportSettings : public NativeAssetImportSettings<ISpriteAtlasImportSettings> {
public:
    virtual ~SpriteAtlasImportSettings() {}
};

class SpriteAtlasMeta : public AssetMetaWithSettings<SpriteAtlasImportSettings> {
public:
    SpriteAtlasMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::SpriteAtlas, guid, path) {}
};

inline IAssetMeta * OnSpriteAtlasAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<SpriteAtlasMeta>(event);
}
