#include "AnimatorComponent.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "Animation/AnimationSampling.h"
#include <set>
#include <algorithm>
#include <cmath>
namespace {bool Same(const GameSproutRuntime::ResourceReference& a,const GameSproutRuntime::ResourceReference& b){return a.Guid==b.Guid&&a.LocalID==b.LocalID;}}
namespace GameSproutRuntime {
AnimatorComponent::AnimatorComponent(IGameObject* object):BuiltinComponent<IAnimatorComponent>(object,"GameSproutRuntime.Animator"){}
Runtime& AnimatorComponent::Owner()const{return dynamic_cast<ObjectAccess*>(_GameObject)->State().Owner;}
void AnimatorComponent::ClearPoses(bool retainClipPose){
    if(!retainClipPose){
        for(auto id:_Applied){auto* object=Owner().QueryGameObject(id);if(object)if(auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());}
        _Applied.clear();
        if(auto* transform=dynamic_cast<TransformComponent*>(_GameObject->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());
    }
    _Pose.clear();for(auto& layer:_Layers){layer.Pose.clear();layer.FrozenPose.clear();layer.PoseCoverage.clear();layer.FrozenCoverage.clear();layer.Frozen=false;}
    _RootMotionDelta={};_AccumulatedMotion={};
}
void AnimatorComponent::OnRelease(){_ClipPlayback={};ClearPoses();Owner().ForgetAnimatorReferences(_GameObject->GetInstanceID());}
void AnimatorComponent::ForgetObject(std::uint64_t id){
    bool changed=false;for(auto& binding:_Bindings)if(binding.ObjectID==id){binding.ObjectID=0;changed=true;}
    if(changed){Stop();_Error="Animator skeleton node was deleted";NotifyComponentChanged(_GameObject,this);}
}
void AnimatorComponent::ConfigurationChanged(){_ClipPlayback={};for(auto& goal:_IKGoals)goal={};_LookAt={};++_SignalVersion;_Layers.clear();ClearPoses();_Generation=0;_SkeletonData=nullptr;_ControllerData=nullptr;_Playing=false;_Stopped=false;_Error.clear();NotifyComponentChanged(_GameObject,this);}
void AnimatorComponent::SetAvatar(const ResourceReference& ref){if(!Same(_Avatar,ref)){_Avatar=ref;ConfigurationChanged();}}
void AnimatorComponent::SetSkeleton(const ResourceReference& ref){if(!Same(_Skeleton,ref)){_Skeleton=ref;ConfigurationChanged();}}
void AnimatorComponent::SetController(const ResourceReference& ref){if(!Same(_Controller,ref)){_Controller=ref;ConfigurationChanged();}}
void AnimatorComponent::SetDefaultClip(const ResourceReference& ref){if(!Same(_DefaultClip,ref)){_DefaultClip=ref;ConfigurationChanged();}}
bool AnimatorComponent::SetNodeBindings(const std::vector<AnimationNodeBinding>& bindings){
    if(bindings.size()>100000)return false;std::set<std::string> paths;std::set<std::uint64_t> ids;
    for(const auto& binding:bindings){if(binding.Path.empty()||!paths.insert(binding.Path).second)return false;
        if(binding.ObjectID){if(_RootMotionMode!=RootMotionMode::Disabled&&binding.ObjectID==_GameObject->GetInstanceID())return false;auto* state=Owner().Find(binding.ObjectID);if(!Owner().IsUsable(state)||state->GetScene()!=_GameObject->GetScene()||state->Object==state->GetScene()||!ids.insert(binding.ObjectID).second)return false;}}
    bool same=bindings.size()==_Bindings.size();if(same)for(std::size_t i=0;i<bindings.size();++i)if(bindings[i].Path!=_Bindings[i].Path||bindings[i].ObjectID!=_Bindings[i].ObjectID){same=false;break;}
    if(same)return true;ClearPoses();_Bindings=bindings;ConfigurationChanged();return true;
}
bool AnimatorComponent::SetSpeed(float value){if(!std::isfinite(value)||value<0)return false;if(_Speed!=value){_Speed=value;NotifyComponentChanged(_GameObject,this);}return true;}
void AnimatorComponent::SetPlayOnStart(bool value){if(value==_PlayOnStart)return;_PlayOnStart=value;NotifyComponentChanged(_GameObject,this);}
void AnimatorComponent::SetLoop(bool value){if(value==_Loop)return;_Loop=value;NotifyComponentChanged(_GameObject,this);}
bool AnimatorComponent::ResolveBone(const std::string& path,std::uint64_t& id)const{
    id=0;for(const auto& binding:_Bindings)if(binding.Path==path){auto* state=Owner().Find(binding.ObjectID);if(!Owner().IsUsable(state)||state->GetScene()!=_GameObject->GetScene())return false;id=binding.ObjectID;return true;}return false;
}
bool AnimatorComponent::Ensure(){
    auto& resources=Owner().GetAnimationResources();const auto generation=resources.GetGeneration();
    if(_Generation==generation&&_SkeletonData&&_ControllerData)return true;
    ++_SignalVersion;_Layers.clear();ClearPoses(_ClipPlayback.Active);_SkeletonData=nullptr;_ControllerData=nullptr;_Error.clear();
    _SkeletonData=resources.Skeleton(_Skeleton,_Error);if(!_SkeletonData)return false;
    _AvatarData=nullptr;if(!_Avatar.Guid.empty()){_AvatarData=resources.Avatar(_Avatar,_Error);if(!_AvatarData||_AvatarData->Skeleton!=_Skeleton){_Error="Target Avatar does not match Animator Skeleton";return false;}}
    if(!_Controller.Guid.empty())_ControllerData=resources.Controller(_Controller,_Error);
    else {if(_DefaultClip.Guid.empty()){_Error="Animator requires a Controller or DefaultClip";return false;}
        _Fallback={};AnimatorState state;state.Name="Default";state.Clip=_DefaultClip;state.Loop=true;AnimatorLayer layer;layer.Name="Base Layer";layer.States.push_back(state);_Fallback.Layers.push_back(layer);_ControllerData=&_Fallback;}
    if(!_ControllerData)return false;
    for(const auto& node:_SkeletonData->Nodes){std::uint64_t id;if(!ResolveBone(node.Path,id)){_Error="Animator skeleton node is missing: "+node.Path;_SkeletonData=nullptr;_ControllerData=nullptr;return false;}}
    _Parameters.clear();for(const auto& p:_ControllerData->Parameters)_Parameters[p.Name]=p.DefaultValue;
    if(!InitializeLayers()){_Layers.clear();_SkeletonData=nullptr;_ControllerData=nullptr;return false;}
    _Generation=generation;_Playing=_PlayOnStart&&!_Stopped;return true;
}
bool AnimatorComponent::SetParameter(const std::string& name,AnimatorParameterType type,double value){
    if(!std::isfinite(value)||!Ensure())return false;
    for(const auto& parameter:_ControllerData->Parameters)if(parameter.Name==name){
        if(parameter.Type!=type)return false;
        const double previous=_Parameters[name];
        std::vector<std::pair<double,double>> durations;
        for(std::uint32_t i=0;i<_Layers.size();++i){const auto& layer=_Layers[i];const auto& definition=_ControllerData->Layers[i];
            const bool current=type==AnimatorParameterType::Float&&layer.Current>=0;
            const bool next=type==AnimatorParameterType::Float&&layer.Next>=0;
            const double a=current?Duration(i,layer.Current):0,b=next?Duration(i,layer.Next):0;
            if(a<0||b<0)return false;durations.push_back({a,b});
        }
        _Parameters[name]=value;
        std::vector<std::pair<double,double>> changed;
        for(std::uint32_t i=0;i<_Layers.size();++i){const auto& layer=_Layers[i];const auto& definition=_ControllerData->Layers[i];
            const bool current=type==AnimatorParameterType::Float&&layer.Current>=0;
            const bool next=type==AnimatorParameterType::Float&&layer.Next>=0;
            const double a=current?Duration(i,layer.Current):0,b=next?Duration(i,layer.Next):0;
            if(a<0||b<0){_Parameters[name]=previous;return false;}changed.push_back({a,b});
        }
        for(std::size_t i=0;i<_Layers.size();++i){auto& layer=_Layers[i];
            if(durations[i].first>0)layer.Time=layer.Time/durations[i].first*changed[i].first;
            if(durations[i].second>0)layer.NextTime=layer.NextTime/durations[i].second*changed[i].second;
        }
        return true;
    }return false;
}
bool AnimatorComponent::SetFloat(const std::string& n,float v){return SetParameter(n,AnimatorParameterType::Float,v);}
bool AnimatorComponent::SetInt(const std::string& n,std::int32_t v){return SetParameter(n,AnimatorParameterType::Int,v);}
bool AnimatorComponent::SetBool(const std::string& n,bool v){return SetParameter(n,AnimatorParameterType::Bool,v?1:0);}
bool AnimatorComponent::SetTrigger(const std::string& n){return SetParameter(n,AnimatorParameterType::Trigger,1);}
bool AnimatorComponent::ResetTrigger(const std::string& n){return SetParameter(n,AnimatorParameterType::Trigger,0);}
bool AnimatorComponent::GetParameter(const std::string& name,double& value)const{if(!const_cast<AnimatorComponent*>(this)->Ensure())return false;const auto found=_Parameters.find(name);if(found==_Parameters.end())return false;value=found->second;return true;}
int AnimatorComponent::StateIndex(std::uint32_t index,const std::string& name)const {
    const auto& layer=_ControllerData->Layers[index];if(name.empty())return int(layer.DefaultState);
    for(std::size_t i=0;i<layer.States.size();++i)if(layer.States[i].Name==name)return int(i);return -1;
}
std::uint32_t AnimatorComponent::GetLayerCount() const {return const_cast<AnimatorComponent*>(this)->Ensure()?std::uint32_t(_Layers.size()):0;}
std::string AnimatorComponent::GetLayerName(std::uint32_t index) const {return const_cast<AnimatorComponent*>(this)->Ensure()&&index<_Layers.size()?_ControllerData->Layers[index].Name:std::string{};}
float AnimatorComponent::GetLayerWeight(std::uint32_t index) const {return const_cast<AnimatorComponent*>(this)->Ensure()&&index<_Layers.size()?_Layers[index].Weight:0;}
bool AnimatorComponent::SetLayerWeight(std::uint32_t index,float weight){
    if(!std::isfinite(weight)||weight<0||weight>1||!Ensure()||index==0||index>=_Layers.size())return false;
    if(_Layers[index].Weight==weight)return true;
    std::vector<std::pair<double,double>> before;for(std::uint32_t i=0;i<_Layers.size();++i)before.push_back({Duration(i,_Layers[i].Current),_Layers[i].Next>=0?Duration(i,_Layers[i].Next):0});
    ++_SignalVersion;_Layers[index].Weight=weight;_RootMotionDelta={};
    for(std::uint32_t i=0;i<_Layers.size();++i){if(before[i].first>0)_Layers[i].Time=_Layers[i].Time/before[i].first*Duration(i,_Layers[i].Current);if(before[i].second>0)_Layers[i].NextTime=_Layers[i].NextTime/before[i].second*Duration(i,_Layers[i].Next);}
    return ApplyPose();
}
bool AnimatorComponent::Play(const std::string& name,float normalized,std::uint32_t index){
    if(!std::isfinite(normalized)||normalized<0||!Ensure()||index>=_Layers.size())return false;
    StopClip();index=SourceLayer(index);const int state=StateIndex(index,name);if(state<0){_Error="Animator state does not exist: "+name;return false;}
    const double duration=Duration(index,state);if(duration<0)return false;
    std::vector<TransformPose> checked;if(!StatePose(index,state,normalized*duration,checked))return false;
    ++_SignalVersion;for(std::uint32_t i=0;i<_Layers.size();++i)if(SourceLayer(i)==index)ExitStates(i);auto& layer=_Layers[index];layer.Current=state;layer.Next=-1;layer.Time=normalized*duration;
    _Playing=true;_Stopped=false;layer.Frozen=false;_Error.clear();
    SynchronizeLayers(index);if(ApplyPose()){EnterState(index,layer.Current);return true;}_Playing=false;ClearPoses();return false;
}
void AnimatorComponent::Resume(){StopClip();if(Ensure()){_Playing=true;_Stopped=false;}}
void AnimatorComponent::Stop(){
    _ClipPlayback={};
    // Direct playback can observe a resource revision while the Controller is
    // frozen. Its old immutable definitions have then been invalidated.
    if(_Generation!=Owner().GetAnimationResources().GetGeneration()){
        _Layers.clear();_ControllerData=nullptr;_SkeletonData=nullptr;_AvatarData=nullptr;_Generation=0;
    }
    for(auto& goal:_IKGoals)goal={};_LookAt={};
    ++_SignalVersion;for(std::uint32_t i=0;i<_Layers.size();++i){ExitStates(i);auto& layer=_Layers[i];layer.Time=0;layer.Next=-1;layer.Frozen=false;}
    _Playing=false;_Stopped=true;ClearPoses();
}
bool AnimatorComponent::Seek(float seconds,std::uint32_t index){
    if(!std::isfinite(seconds)||seconds<0||!Ensure()||index>=_Layers.size())return false;
    index=SourceLayer(index);++_SignalVersion;auto& layer=_Layers[index];if(layer.Next>=0)ExitState(index,layer.Next);layer.StartMarkers.clear();_RootMotionDelta={};layer.Time=seconds;layer.Next=-1;layer.Frozen=false;
    for(std::uint32_t i=0;i<_Layers.size();++i)if(SourceLayer(i)==index)_Layers[i].StartMarkers.clear();
    SynchronizeLayers(index);return ApplyPose();
}
AnimatorPlaybackState AnimatorComponent::GetPlaybackState(std::uint32_t index)const{
    AnimatorPlaybackState result;
    if(!const_cast<AnimatorComponent*>(this)->Ensure()||index>=_Layers.size())return result;
    const auto& layer=_Layers[index];const auto& definition=_ControllerData->Layers[index];result.Playing=_Playing;result.Time=float(layer.Time);
    if(layer.Current>=0){result.CurrentState=definition.States[layer.Current].Name;if(layer.Next>=0)result.NextState=definition.States[layer.Next].Name;
        const double duration=const_cast<AnimatorComponent*>(this)->Duration(index,layer.Current);if(duration>0)result.NormalizedTime=float(layer.Time/duration);
        result.TransitionWeight=layer.Next>=0&&layer.TransitionSeconds>0?std::clamp(layer.TransitionElapsed/layer.TransitionSeconds,0.f,1.f):0;
    }return result;
}
}
