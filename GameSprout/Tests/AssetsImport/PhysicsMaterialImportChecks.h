#pragma once
#include "../../Module/Shared/PhysicsMaterialDefinition.h"

// Uses production Pipeline/Library with only temporary project files.
template<class Import>
static void CheckPhysicsMaterialImport(const QDir& root,ILibraryManager* library,const Import& import) {
    std::string error;GameSproutRuntime::PhysicsMaterialData data;
    data.Friction=0.12f;data.Restitution=0.7f;data.FrictionCombine=GameSproutRuntime::PhysicsMaterialCombine::Minimum;
    const auto source=PhysicsMaterialDefinition::Write(data);
    Check(AssetArtifactIO::Write(root.filePath("Assets/Ice.physicmaterial"),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)),error),error);
    TestMeta meta(eAssetImporterType::PhysicsMaterial,Guid(101,102,103,104),"Ice.physicmaterial");meta.Settings["MainObjectID"]=Json::UInt64(71);
    const auto imported=import(meta);
    Check(imported.Success&&imported.Manifest.ImporterVersion==3&&imported.Manifest.Assets.size()==1,"Physical material import failed: "+imported.Error);
    auto* asset=library->QueryAsset(meta._Guid,71);QByteArray payload;const auto descriptor=ReadArtifact(asset,&payload);
    Check(asset&&asset->GetAssetType()==eAssetType::PhysicsMaterial&&payload.size()==16&&asset->GetDataSize()==16,"Physical material contains a JSON/container header or lost its requested LocalID");
    Check(descriptor["Encoding"]=="RawBytes"&&descriptor["Format"]=="PhysicsMaterial1"&&descriptor["Layout"]=="F32F32U32U32LE","Physical material descriptor lost its concrete binary layout");
    GameSproutRuntime::PhysicsMaterialData restored;
    Check(PhysicsMaterialDefinition::Decode(reinterpret_cast<const std::uint8_t*>(payload.constData()),static_cast<std::size_t>(payload.size()),restored,error),error);
    Check(restored.Friction==data.Friction&&restored.Restitution==data.Restitution&&restored.FrictionCombine==data.FrictionCombine,"Prepared surface differs from the authored material");
    const auto cached=import(meta);Check(cached.Success&&cached.Cached&&cached.Manifest.Assets[0].LocalID==71,"Physical material cache changed its identity");
    std::string metaJson;Check(meta.SerializeMeta(metaJson)&&AssetArtifactIO::Write(root.filePath("Assets/Ice.physicmaterial.meta"),QByteArray::fromStdString(metaJson),error),error);
}
