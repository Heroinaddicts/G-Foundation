#pragma once

#include "AssetFactoryCommon.hpp"

class ShaderIncludeImportSettings : public EmptyAssetImportSettings<IShaderIncludeImportSettings> {
public:
    virtual ~ShaderIncludeImportSettings() {}
};

class ShaderIncludeMeta : public AssetMetaWithSettings<ShaderIncludeImportSettings> {
public:
    ShaderIncludeMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::ShaderInclude, guid, path) {}
};

inline IAssetMeta * OnShaderIncludeAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<ShaderIncludeMeta>(event);
}
