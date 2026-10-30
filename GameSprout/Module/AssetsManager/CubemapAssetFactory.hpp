#pragma once

#include "AssetFactoryCommon.hpp"

class CubemapImportSettings : public NativeAssetImportSettings<ICubemapImportSettings> {
public:
    virtual ~CubemapImportSettings() {}
};

class CubemapMeta : public AssetMetaWithSettings<CubemapImportSettings> {
public:
    CubemapMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Cubemap, guid, path) {}
};

inline IAssetMeta * OnCubemapAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<CubemapMeta>(event);
}
