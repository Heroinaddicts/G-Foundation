#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "../Shared/AnimationDefinition.h"

bool LibraryRuntimeResources::LoadAnimatorController(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AnimatorControllerData& output,std::string& error) {
    output={};Json::Value descriptor,body;QByteArray bytes;
    if(!Read(reference,eAssetType::AnimatorController,descriptor,bytes,error))return false;
    if(descriptor["Format"]!="AnimatorController3"||descriptor["Encoding"]!="BinaryValueLE1"||!AssetValueBinary::Decode(bytes,body)
        ||!AnimationDefinition::ReadController(body,true,output,error)) {
        if(error.empty())error="Invalid compiled AnimatorController";
        return false;
    }
    return true;
}

#include "../Shared/AvatarMaskDefinition.h"
bool LibraryRuntimeResources::LoadAvatarMask(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AvatarMaskData& output,std::string& error) {
    output={};Json::Value descriptor,body;QByteArray bytes;GameSproutRuntime::AvatarMaskData data;
    if(!Read(reference,eAssetType::AvatarMask,descriptor,bytes,error))return false;
    if(descriptor["Format"]!="AvatarMask2"||descriptor["Encoding"]!="BinaryValueLE1"||!AssetValueBinary::Decode(bytes,body)
        ||!AvatarMaskDefinition::Read(body,true,data,error)) {
        if(error.empty())error="Invalid compiled AvatarMask";return false;
    }
    GameSproutRuntime::SkeletonData skeleton;
    if((data.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!LoadSkeleton(data.Skeleton,skeleton,error))||!AvatarMaskDefinition::Validate(data,skeleton,error))return false;
    output=std::move(data);return true;
}
