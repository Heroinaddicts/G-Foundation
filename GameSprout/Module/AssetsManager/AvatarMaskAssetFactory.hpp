#pragma once

#include "AssetFactoryCommon.hpp"

class AvatarMaskImportSettings : public NativeAssetImportSettings<IAvatarMaskImportSettings> {
public:
    virtual ~AvatarMaskImportSettings() {}
};

class AvatarMaskMeta : public AssetMetaWithSettings<AvatarMaskImportSettings> {
public:
    AvatarMaskMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::AvatarMask, guid, path) {}
};

inline IAssetMeta * OnAvatarMaskAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AvatarMaskMeta>(event);
}
