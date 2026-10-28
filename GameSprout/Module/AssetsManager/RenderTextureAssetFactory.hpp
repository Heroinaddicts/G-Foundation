#pragma once

#include "AssetFactoryCommon.hpp"

class RenderTextureImportSettings : public NativeAssetImportSettings<IRenderTextureImportSettings> {
public:
    virtual ~RenderTextureImportSettings() {}
};

class RenderTextureMeta : public AssetMetaWithSettings<RenderTextureImportSettings> {
public:
    RenderTextureMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::RenderTexture, guid, path) {}
};

inline IAssetMeta * OnRenderTextureAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<RenderTextureMeta>(event);
}
