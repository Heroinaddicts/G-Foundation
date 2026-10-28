#pragma once

#include "AssetFactoryCommon.hpp"

class AnimationClipImportSettings : public NativeAssetImportSettings<IAnimationClipImportSettings> {
public:
    virtual ~AnimationClipImportSettings() {}
};

class AnimationClipMeta : public AssetMetaWithSettings<AnimationClipImportSettings> {
public:
    AnimationClipMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::AnimationClip, guid, path) {}
};

inline IAssetMeta * OnAnimationClipAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AnimationClipMeta>(event);
}
