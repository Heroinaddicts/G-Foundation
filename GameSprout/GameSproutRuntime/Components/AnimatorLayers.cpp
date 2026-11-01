#include "AnimatorComponent.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "Animation/AnimationSampling.h"
#include "Animation/AnimationMotion.h"
#include "RuntimeMath.h"
#include "Api/HumanoidValidation.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace GameSproutRuntime {
namespace {
Quaternion Product(const Quaternion& a,const Quaternion& b){
    Quaternion result{a.W*b.X+a.X*b.W+a.Y*b.Z-a.Z*b.Y,a.W*b.Y-a.X*b.Z+a.Y*b.W+a.Z*b.X,
        a.W*b.Z+a.X*b.Y-a.Y*b.X+a.Z*b.W,a.W*b.W-a.X*b.X-a.Y*b.Y-a.Z*b.Z};
    result.TryNormalize(result);return result;
}
bool NonzeroScale(const Vector3& scale){return std::fabs(scale.X)>1e-6f&&std::fabs(scale.Y)>1e-6f&&std::fabs(scale.Z)>1e-6f;}
TransformPose Additive(const TransformPose& lower,const TransformPose& pose,const TransformPose& reference,float weight){
    TransformPose result=lower;result.Position=lower.Position+(pose.Position-reference.Position)*weight;
    Quaternion inverse;reference.Rotation.TryNormalize(inverse);inverse={-inverse.X,-inverse.Y,-inverse.Z,inverse.W};
    TransformPose identity,delta;delta.Rotation=Product(inverse,pose.Rotation);
    result.Rotation=Product(lower.Rotation,AnimationSampling::Blend(identity,delta,weight).Rotation);
    // Local scale is multiplicative: reference 2, animated 4 yields a 2x contribution at weight 1.
    result.Scale={lower.Scale.X*(1+(pose.Scale.X/reference.Scale.X-1)*weight),
        lower.Scale.Y*(1+(pose.Scale.Y/reference.Scale.Y-1)*weight),lower.Scale.Z*(1+(pose.Scale.Z/reference.Scale.Z-1)*weight)};
    return result;
}
}
bool AnimatorComponent::InitializeLayers(){
    _Layers.clear();_Layers.resize(_ControllerData->Layers.size());
    for(std::size_t index=0;index<_Layers.size();++index){auto& layer=_Layers[index];const auto& definition=_ControllerData->Layers[index];
        layer.Current=int(definition.DefaultState);layer.Weight=definition.DefaultWeight;
        layer.MaskWeights.assign(_SkeletonData->Nodes.size(),1);
        if(!definition.Mask.Guid.empty()){
            const auto* mask=Owner().GetAnimationResources().Mask(definition.Mask,_Error);if(!mask)return false;
            if(mask->Mode==AvatarMaskMode::Humanoid){
                if(!_AvatarData){_Error="Humanoid mask requires target Avatar";return false;}
                std::map<std::string,float> weights;
                for(const auto& mapping:_AvatarData->Bones){float weight=mask->DefaultWeight;const auto part=HumanBoneBodyPart(mapping.Bone);for(const auto& value:mask->HumanParts)if(value.Part==part)weight=value.Weight;weights[mapping.Path]=weight;}
                std::vector<bool> resolved(_SkeletonData->Nodes.size());
                for(std::size_t i=0;i<resolved.size();++i){std::vector<int> chain;int at=int(i);while(at>=0&&!resolved[at]){chain.push_back(at);at=_SkeletonData->Nodes[at].Parent;}
                    while(!chain.empty()){const int node=chain.back();chain.pop_back();const auto found=weights.find(_SkeletonData->Nodes[node].Path);const int parent=_SkeletonData->Nodes[node].Parent;
                        layer.MaskWeights[node]=found!=weights.end()?found->second:(parent<0?mask->DefaultWeight:layer.MaskWeights[parent]);resolved[node]=true;}}
            }else{
            if(mask->Skeleton.Guid!=_Skeleton.Guid||mask->Skeleton.LocalID!=_Skeleton.LocalID){_Error="AvatarMask Skeleton differs from Animator Skeleton: "+definition.Name;return false;}
            std::map<std::string,float> overrides;for(const auto& node:mask->Nodes)overrides.emplace(node.Path,node.Weight);
            std::map<std::string,std::size_t> paths;for(std::size_t i=0;i<_SkeletonData->Nodes.size();++i)paths.emplace(_SkeletonData->Nodes[i].Path,i);
            for(const auto& node:mask->Nodes)if(!paths.count(node.Path)){_Error="AvatarMask path does not belong to Skeleton: "+node.Path;return false;}
            // Skeleton storage need not be parent-first. Resolve each chain once, iteratively.
            std::vector<bool> resolved(_SkeletonData->Nodes.size(),false);
            for(std::size_t i=0;i<resolved.size();++i){std::vector<std::size_t> chain;std::int32_t at=std::int32_t(i);
                while(at>=0&&!resolved[at]){chain.push_back(std::size_t(at));at=_SkeletonData->Nodes[at].Parent;}
                while(!chain.empty()){const auto node=chain.back();chain.pop_back();const auto found=overrides.find(_SkeletonData->Nodes[node].Path);
                    const auto parent=_SkeletonData->Nodes[node].Parent;
                    layer.MaskWeights[node]=found!=overrides.end()?found->second:(parent<0?mask->DefaultWeight:layer.MaskWeights[parent]);resolved[node]=true;
                }
            }
            }
            layer.RootMotionWeight=mask->RootMotionWeight;
        }
        layer.ReferencePose.reserve(_SkeletonData->Nodes.size());for(const auto& node:_SkeletonData->Nodes)layer.ReferencePose.push_back(node.Transform);
        if(definition.BlendingMode==AnimatorLayerBlendingMode::Additive){
            if(!definition.ReferenceClip.Guid.empty()){
                const auto* clip=Owner().GetAnimationResources().Clip(definition.ReferenceClip,_Error);if(!clip)return false;
                std::vector<float> coverage;if(!SampleClip(*clip,definition.ReferenceTime,false,layer.ReferencePose,coverage))return false;
            }
            for(const auto& pose:layer.ReferencePose)if(!NonzeroScale(pose.Scale)){_Error="Additive reference scale cannot contain zero: "+definition.Name;return false;}
        }
    }
    return true;
}
bool AnimatorComponent::LayerPose(std::uint32_t index,const std::vector<TransformPose>& lower,std::vector<TransformPose>& output){
    auto& layer=_Layers[index];const auto& definition=_ControllerData->Layers[index];
    const auto& baseline=definition.BlendingMode==AnimatorLayerBlendingMode::Additive?layer.ReferencePose:lower;
    std::vector<TransformPose> sampled;std::vector<float> coverage;
    if(layer.Frozen){sampled=layer.FrozenPose;coverage=layer.FrozenCoverage;}
    else if(!StatePose(index,layer.Current,layer.Time,sampled,&baseline,&coverage))return false;
    if(layer.Next>=0){std::vector<TransformPose> next;std::vector<float> nextCoverage;if(!StatePose(index,layer.Next,layer.NextTime,next,&baseline,&nextCoverage))return false;
        const float weight=layer.TransitionSeconds>0?std::clamp(layer.TransitionElapsed/layer.TransitionSeconds,0.f,1.f):1;
        AnimationSampling::BlendContributions(sampled,coverage,next,nextCoverage,weight);
    }
    layer.Pose=sampled;layer.PoseCoverage=coverage;output=lower;
    for(std::size_t i=0;i<output.size();++i){const float weight=layer.Weight*layer.MaskWeights[i]*coverage[i];if(weight==0)continue;
        output[i]=definition.BlendingMode==AnimatorLayerBlendingMode::Additive?Additive(lower[i],sampled[i],layer.ReferencePose[i],weight):AnimationSampling::Blend(lower[i],sampled[i],weight);
    }
    return true;
}
bool AnimatorComponent::ApplyPose(){
    if(_ClipPlayback.Active)return true;
    if(!_ControllerData||!_SkeletonData||_Layers.empty())return false;
    std::vector<TransformPose> output;for(const auto& node:_SkeletonData->Nodes)output.push_back(node.Transform);
    for(std::uint32_t index=0;index<_Layers.size();++index){if(_Layers[index].Weight==0)continue;
        std::vector<TransformPose> next;if(!LayerPose(index,output,next))return false;output=std::move(next);
    }
    if(!ApplyHumanIK(output,_DispatchIK))return false;
    if(_IKAborted||(_DispatchIK&&!_Playing))return true;
    std::vector<TransformComponent*> transforms;std::vector<Mat4> matrices;transforms.reserve(output.size());matrices.reserve(output.size());
    for(std::size_t i=0;i<output.size();++i){std::uint64_t id=0;if(!ResolveBone(_SkeletonData->Nodes[i].Path,id)){_Error="Animator binding was removed: "+_SkeletonData->Nodes[i].Path;ClearPoses();_Playing=false;return false;}
        auto* object=Owner().QueryGameObject(id);auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform());if(!transform){_Error="Animator requires Runtime Transform nodes";return false;}
        transforms.push_back(transform);matrices.push_back(RuntimeMath::ComposeTRS(output[i].Position,output[i].Rotation,output[i].Scale));
    }
    _Applied.clear();for(std::size_t i=0;i<output.size();++i){if(!transforms[i]->SetEvaluatedPose(_GameObject->GetInstanceID(),matrices[i])){_Error="Skeleton pose is owned by another Animator";ClearPoses();_Playing=false;return false;}_Applied.push_back(transforms[i]->GetGameObject()->GetInstanceID());}
    _Pose=std::move(output);return true;
}
void AnimatorComponent::Update(IGameSproutRuntime*,float delta){
    _RootMotionDelta={};_MotionEventCount=0;_ConsumedTriggers.clear();
    if(_ClipPlayback.Active){UpdateClipPlayback(delta);return;}
    if(!Ensure()||!_Playing)return;
    for(auto& layer:_Layers){layer.MotionDelta={};layer.HasRootMotion=false;}
    for(std::uint32_t index=0;index<_Layers.size();++index)if(_ControllerData->Layers[index].SyncedLayerIndex<0&&!UpdateLayer(index,delta))return;
    // All layers observe the same parameter snapshot, including shared trigger pulses.
    for(const auto& trigger:_ConsumedTriggers)_Parameters[trigger]=0;
    const auto version=_SignalVersion;_DispatchIK=true;const bool applied=ApplyPose();_DispatchIK=false;
    if(version!=_SignalVersion||_IKAborted)return;
    if(!applied){MotionFailure(_Error);return;}
    if(delta>0&&_Speed>0&&_RootMotionMode!=RootMotionMode::Disabled){
        for(std::size_t index=0;index<_Layers.size();++index){const auto& layer=_Layers[index];const float weight=layer.Weight*layer.RootMotionWeight;if(weight==0||!layer.HasRootMotion)continue;
            if(_ControllerData->Layers[index].BlendingMode==AnimatorLayerBlendingMode::Override)_RootMotionDelta=AnimationMotion::Blend(_RootMotionDelta,layer.MotionDelta,weight);
            else _RootMotionDelta=AnimationMotion::Compose(_RootMotionDelta,AnimationMotion::Blend({},layer.MotionDelta,weight));
        }
        RootMotionNotification notification;notification.SceneID=_GameObject->GetScene()->GetInstanceID();notification.ObjectID=_GameObject->GetInstanceID();notification.Delta=_RootMotionDelta;notification.Mode=_RootMotionMode;
        if(!Owner().QueueRootMotion(*this,notification))MotionFailure("Animation root-motion queue exceeded 8192 pending records");
    }
}
}
