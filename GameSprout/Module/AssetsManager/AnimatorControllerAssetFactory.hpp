#pragma once

#include "AssetFactoryCommon.hpp"

class AnimatorControllerImportSettings : public NativeAssetImportSettings<IAnimatorControllerImportSettings> {
public:
    virtual ~AnimatorControllerImportSettings() {}
};

class AnimatorControllerMeta : public AssetMetaWithSettings<AnimatorControllerImportSettings> {
public:
    AnimatorControllerMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::AnimatorController, guid, path) {}
};

inline IAssetMeta * OnAnimatorControllerAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AnimatorControllerMeta>(event);
}
