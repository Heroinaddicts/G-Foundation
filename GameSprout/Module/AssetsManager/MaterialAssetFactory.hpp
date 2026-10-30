#pragma once

#include "AssetFactoryCommon.hpp"

class MaterialImportSettings : public NativeAssetImportSettings<IMaterialImportSettings> {
public:
    virtual ~MaterialImportSettings() {}
};

class MaterialMeta : public AssetMetaWithSettings<MaterialImportSettings> {
public:
    MaterialMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Material, guid, path) {}
};

inline IAssetMeta * OnMaterialAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<MaterialMeta>(event);
}
