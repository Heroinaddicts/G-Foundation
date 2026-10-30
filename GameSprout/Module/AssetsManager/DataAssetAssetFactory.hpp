#pragma once

#include "AssetFactoryCommon.hpp"

class DataAssetImportSettings : public NativeAssetImportSettings<IDataAssetImportSettings> {
public:
    virtual ~DataAssetImportSettings() {}
};

class DataAssetMeta : public AssetMetaWithSettings<DataAssetImportSettings> {
public:
    DataAssetMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::DataAsset, guid, path) {}
};

inline IAssetMeta * OnDataAssetAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<DataAssetMeta>(event);
}
