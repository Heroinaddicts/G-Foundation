#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "../Shared/PhysicsMaterialDefinition.h"

bool LibraryRuntimeResources::LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PhysicsMaterialData& output,std::string& error) {
    output={};Json::Value descriptor;QByteArray bytes;
    if(!Read(reference,eAssetType::PhysicsMaterial,descriptor,bytes,error))return false;
    if(descriptor["Encoding"]!="RawBytes"||descriptor["Format"]!="PhysicsMaterial1"||descriptor["Layout"]!="F32F32U32U32LE") {
        error="Invalid prepared PhysicsMaterial descriptor";return false;
    }
    return PhysicsMaterialDefinition::Decode(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()),output,error);
}
