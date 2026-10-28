#pragma once

#include "AssetFactoryCommon.hpp"

class ShaderVariantCollectionImportSettings : public NativeAssetImportSettings<IShaderVariantCollectionImportSettings> {
public:
    virtual ~ShaderVariantCollectionImportSettings() {}
};

class ShaderVariantCollectionMeta : public AssetMetaWithSettings<ShaderVariantCollectionImportSettings> {
public:
    ShaderVariantCollectionMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::ShaderVariantCollection, guid, path) {}
};

inline IAssetMeta * OnShaderVariantCollectionAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<ShaderVariantCollectionMeta>(event);
}
