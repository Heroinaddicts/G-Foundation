#include "AnimationResources.h"
#include "AnimationSampling.h"
#include "Api/HumanoidValidation.h"
#include <cmath>
#include <set>
namespace {std::string Key(const GameSproutRuntime::ResourceReference& ref){return ref.Guid+":"+std::to_string(ref.LocalID);}}
namespace GameSproutRuntime {
void AnimationResources::SetProvider(IRuntimeResourceProvider* provider){if(_Provider==provider)return;_Provider=provider;_Revision=0;_Skeletons.clear();_Clips.clear();_Controllers.clear();_Masks.clear();_Avatars.clear();++_Generation;}
void AnimationResources::Sync(){const auto revision=_Provider?_Provider->GetRevision():0;if(revision==_Revision)return;_Revision=revision;_Skeletons.clear();_Clips.clear();_Controllers.clear();_Masks.clear();_Avatars.clear();++_Generation;}
std::uint64_t AnimationResources::GetGeneration(){Sync();return _Generation;}
const SkeletonData* AnimationResources::Skeleton(const ResourceReference& ref,std::string& error){Sync();auto found=_Skeletons.find(Key(ref));if(found!=_Skeletons.end())return &found->second;SkeletonData data;if(!_Provider){error="Runtime resource provider is not attached";return nullptr;}if(!_Provider->LoadSkeleton(ref,data,error)||!AnimationSampling::Validate(data,error))return nullptr;return &_Skeletons.emplace(Key(ref),std::move(data)).first->second;}
const AnimationClipData* AnimationResources::Clip(const ResourceReference& ref,std::string& error){Sync();auto found=_Clips.find(Key(ref));if(found!=_Clips.end())return &found->second;AnimationClipData data;if(!_Provider){error="Runtime resource provider is not attached";return nullptr;}if(!_Provider->LoadAnimationClip(ref,data,error)||!AnimationSampling::Validate(data,error))return nullptr;return &_Clips.emplace(Key(ref),std::move(data)).first->second;}
const AnimatorControllerData* AnimationResources::Controller(const ResourceReference& ref,std::string& error){Sync();auto found=_Controllers.find(Key(ref));if(found!=_Controllers.end())return &found->second;AnimatorControllerData data;if(!_Provider){error="Runtime resource provider is not attached";return nullptr;}if(!_Provider->LoadAnimatorController(ref,data,error)||!AnimationSampling::Validate(data,error))return nullptr;
    // Resolve synchronization once per immutable Controller, not once per Animator instance.
    for(std::size_t index=0;index<data.Layers.size();++index){auto& layer=data.Layers[index];if(layer.SyncedLayerIndex<0)continue;const auto& source=data.Layers[layer.SyncedLayerIndex];
        layer.States=source.States;layer.Transitions=source.Transitions;layer.DefaultState=source.DefaultState;
        std::map<std::string,std::size_t> states;for(std::size_t state=0;state<layer.States.size();++state)states[layer.States[state].Name]=state;
        for(const auto& value:layer.MotionOverrides){auto& state=layer.States[states.at(value.State)];state.Clip=value.Clip;state.BlendParameter=value.BlendParameter;state.Motions=value.Motions;}}
    return &_Controllers.emplace(Key(ref),std::move(data)).first->second;}
const AvatarMaskData* AnimationResources::Mask(const ResourceReference& ref,std::string& error){
    Sync();auto found=_Masks.find(Key(ref));if(found!=_Masks.end())return &found->second;
    AvatarMaskData data;
    if(!_Provider){error="Runtime resource provider is not attached";return nullptr;}
    if(!_Provider->LoadAvatarMask(ref,data,error))return nullptr;
    if(data.Mode>AvatarMaskMode::Humanoid||!std::isfinite(data.DefaultWeight)||data.DefaultWeight<0||data.DefaultWeight>1||!std::isfinite(data.RootMotionWeight)||data.RootMotionWeight<0||data.RootMotionWeight>1||data.Nodes.size()>100000){error="Invalid AvatarMask declaration";return nullptr;}
    if(data.Mode==AvatarMaskMode::Transform){if(data.Skeleton.Guid.empty()||!data.Skeleton.LocalID||!data.HumanParts.empty()){error="Transform mask requires Skeleton and no human parts";return nullptr;}}
    else{if(!data.Skeleton.Guid.empty()||data.Skeleton.LocalID||!data.Nodes.empty()){error="Humanoid mask cannot bind Skeleton paths";return nullptr;}
        std::set<HumanBodyPart> parts;for(const auto& part:data.HumanParts)if(part.Part>=HumanBodyPart::Count||!parts.insert(part.Part).second||!std::isfinite(part.Weight)||part.Weight<0||part.Weight>1){error="Invalid humanoid body-part mask";return nullptr;}}
    std::set<std::string> paths;for(const auto& node:data.Nodes)if(node.Path.empty()||!paths.insert(node.Path).second||!std::isfinite(node.Weight)||node.Weight<0||node.Weight>1){error="Invalid AvatarMask node";return nullptr;}
    return &_Masks.emplace(Key(ref),std::move(data)).first->second;
}

}

namespace GameSproutRuntime {
const HumanoidAvatarData* AnimationResources::Avatar(const ResourceReference& ref,std::string& error){
 Sync();auto found=_Avatars.find(Key(ref));if(found!=_Avatars.end())return &found->second;
 HumanoidAvatarData data;if(!_Provider||!_Provider->LoadHumanoidAvatar(ref,data,error))return nullptr;
 const auto* skeleton=Skeleton(data.Skeleton,error);if(!skeleton||!ValidateHumanAvatar(*skeleton,data,error))return nullptr;
 return &_Avatars.emplace(Key(ref),std::move(data)).first->second;
}
}
