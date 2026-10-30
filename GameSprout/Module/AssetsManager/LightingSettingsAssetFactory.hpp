#pragma once

#include "AssetFactoryCommon.hpp"

class LightingSettingsImportSettings : public NativeAssetImportSettings<ILightingSettingsImportSettings> {
public:
    virtual ~LightingSettingsImportSettings() {}
};

class LightingSettingsMeta : public AssetMetaWithSettings<LightingSettingsImportSettings> {
public:
    LightingSettingsMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::LightingSettings, guid, path) {}
};

inline IAssetMeta * OnLightingSettingsAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<LightingSettingsMeta>(event);
}
