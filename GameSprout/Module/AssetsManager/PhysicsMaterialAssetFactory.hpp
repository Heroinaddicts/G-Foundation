#pragma once

#include "AssetFactoryCommon.hpp"

class PhysicsMaterialImportSettings : public NativeAssetImportSettings<IPhysicsMaterialImportSettings> {
public:
    virtual ~PhysicsMaterialImportSettings() {}
};

class PhysicsMaterialMeta : public AssetMetaWithSettings<PhysicsMaterialImportSettings> {
public:
    PhysicsMaterialMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetType::PhysicsMaterial, guid, path) {}
};

inline IAssetMeta * OnPhysicsMaterialAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<PhysicsMaterialMeta>(event);
}
