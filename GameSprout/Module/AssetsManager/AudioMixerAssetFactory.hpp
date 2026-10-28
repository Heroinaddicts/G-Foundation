#pragma once

#include "AssetFactoryCommon.hpp"

class AudioMixerImportSettings : public NativeAssetImportSettings<IAudioMixerImportSettings> {
public:
    virtual ~AudioMixerImportSettings() {}
};

class AudioMixerMeta : public AssetMetaWithSettings<AudioMixerImportSettings> {
public:
    AudioMixerMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::AudioMixer, guid, path) {}
};

inline IAssetMeta * OnAudioMixerAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AudioMixerMeta>(event);
}
