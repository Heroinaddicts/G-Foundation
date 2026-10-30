#pragma once

#include "AssetFactoryCommon.hpp"

class PhysicsMaterial2DImportSettings : public NativeAssetImportSettings<IPhysicsMaterial2DImportSettings> {
public:
    virtual ~PhysicsMaterial2DImportSettings() {}
};

class PhysicsMaterial2DMeta : public AssetMetaWithSettings<PhysicsMaterial2DImportSettings> {
public:
    PhysicsMaterial2DMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::PhysicsMaterial2D, guid, path) {}
};

inline IAssetMeta * OnPhysicsMaterial2DAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<PhysicsMaterial2DMeta>(event);
}
