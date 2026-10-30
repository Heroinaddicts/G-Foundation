#pragma once

#include "AssetFactoryCommon.hpp"

class AnimatorOverrideControllerImportSettings : public NativeAssetImportSettings<IAnimatorOverrideControllerImportSettings> {
public:
    virtual ~AnimatorOverrideControllerImportSettings() {}
};

class AnimatorOverrideControllerMeta : public AssetMetaWithSettings<AnimatorOverrideControllerImportSettings> {
public:
    AnimatorOverrideControllerMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::AnimatorOverrideController, guid, path) {}
};

inline IAssetMeta * OnAnimatorOverrideControllerAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AnimatorOverrideControllerMeta>(event);
}
