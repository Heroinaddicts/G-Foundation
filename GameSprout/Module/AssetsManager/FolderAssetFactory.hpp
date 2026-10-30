#pragma once

#include "AssetFactoryCommon.hpp"

class FolderImportSettings : public EmptyAssetImportSettings<IFolderImportSettings> {
public:
    virtual ~FolderImportSettings() {}
};

class FolderMeta : public AssetMetaWithSettings<FolderImportSettings> {
public:
    FolderMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::Folder, guid, path) {}
};

inline IAssetMeta * OnFolderAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<FolderMeta>(event);
}
