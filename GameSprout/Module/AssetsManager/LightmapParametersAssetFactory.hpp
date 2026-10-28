#pragma once

#include "AssetFactoryCommon.hpp"

class LightmapParametersImportSettings : public NativeAssetImportSettings<ILightmapParametersImportSettings> {
public:
    virtual ~LightmapParametersImportSettings() {}
};

class LightmapParametersMeta : public AssetMetaWithSettings<LightmapParametersImportSettings> {
public:
    LightmapParametersMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::LightmapParameters, guid, path) {}
};

inline IAssetMeta * OnLightmapParametersAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<LightmapParametersMeta>(event);
}
