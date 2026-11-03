#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "ILibraryManager.h"
#include "../Shared/PhysicsMaterialDefinition.h"

bool LibraryRuntimeResources::LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PhysicsMaterialData& output,std::string& error) {
    output={};error.clear();
    ObserveResource(reference);
    const Guid guid(reference.Guid);
    // A deleted source/subasset retains its reference in Scene/Prefab data.
    // Missing identity uses the default surface; a present but corrupt artifact
    // must still fail the normal descriptor/byte validation below.
    if(_Library&&guid.IsValid()&&reference.LocalID&&!_Library->QueryAsset(guid,reference.LocalID))return true;
    Json::Value descriptor;QByteArray bytes;
    if(!Read(reference,eAssetType::PhysicsMaterial,descriptor,bytes,error))return false;
    if(descriptor["Encoding"]!="RawBytes"||descriptor["Format"]!="PhysicsMaterial1"||descriptor["Layout"]!="F32F32U32U32LE") {
        error="Invalid prepared PhysicsMaterial descriptor";return false;
    }
    return PhysicsMaterialDefinition::Decode(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()),output,error);
}
