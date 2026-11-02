#pragma once
#include "AssetFactoryCommon.hpp"
class PrefabImportSettings : public NativeAssetImportSettings<IPrefabImportSettings> {};
class PrefabMeta : public AssetMetaWithSettings<PrefabImportSettings> {
public:
    PrefabMeta(const Guid& guid,const std::string& path):AssetMetaWithSettings(eAssetImporterType::Prefab,guid,path) {}
};
inline IAssetMeta* OnPrefabAssetCreate(const Foundation::FileSystemWatcherEvent& event,
    const std::string&,const std::string&,void* const) {return CreateAssetMeta<PrefabMeta>(event);}
