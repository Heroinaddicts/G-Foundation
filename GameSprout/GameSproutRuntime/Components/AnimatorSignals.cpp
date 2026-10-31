#include "AnimatorComponent.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "ComponentFactory.h"
#include "Animation/AnimationMotion.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
bool AnimatorComponent::SetRootMotionMode(RootMotionMode mode) {
    if(mode>RootMotionMode::ApplyToObject)return false;
    if(_RootMotionMode==mode)return true;
    if(mode!=RootMotionMode::Disabled)for(const auto& binding:_Bindings)if(binding.ObjectID==_GameObject->GetInstanceID())return false;
    _RootMotionMode=mode;ConfigurationChanged();return true;
}
void AnimatorComponent::MotionFailure(const std::string& error) {
    ++_SignalVersion;for(auto& layer:_Layers){layer.EnteredStates.clear();layer.StartMarkers.clear();}_Playing=false;_Stopped=true;ClearPoses();_Error=error;
}
void AnimatorComponent::BeginRootMotionDispatch(){_RootDispatchActive=true;_RootDispatchSubmitted=false;_RootDispatchVersion=_SignalVersion;}
bool AnimatorComponent::EndRootMotionDispatch(RootMotionDelta& solved){
    _RootDispatchActive=false;
    const bool submitted=_RootDispatchSubmitted&&_RootDispatchVersion==_SignalVersion;
    if(submitted)solved=_SolvedMotion;_RootDispatchSubmitted=false;return submitted;
}
bool AnimatorComponent::ApplyRootMotionDelta(const RootMotionDelta& solved){
    Quaternion rotation;
    if(!_RootDispatchActive||_RootDispatchSubmitted||_RootDispatchVersion!=_SignalVersion||!_Playing||
        !RuntimeMath::IsFinite(solved.Translation)||!solved.Rotation.TryNormalize(rotation))return false;
    _SolvedMotion={solved.Translation,rotation};_RootDispatchSubmitted=true;return true;
}
bool AnimatorComponent::ApplyRootMotion(const RootMotionDelta& delta) {
    auto* transform=dynamic_cast<TransformComponent*>(_GameObject->GetTransform());if(!transform)return false;
    const auto accumulated=AnimationMotion::Compose(_AccumulatedMotion,delta);
    const Mat4 authoring=RuntimeMath::ComposeTRS(transform->GetLocalPosition(),transform->GetLocalRotation(),transform->GetLocalScale());
    if(!transform->SetEvaluatedPose(_GameObject->GetInstanceID(),authoring*AnimationMotion::Matrix(accumulated)))return false;
    _AccumulatedMotion=accumulated;return true;
}
void AnimatorComponent::EnterState(std::uint32_t layerIndex,int state) {
    if(!_ControllerData||layerIndex>=_Layers.size()||_Generation!=Owner().GetAnimationResources().GetCachedGeneration()||state<0)return;
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    if(!layer.EnteredStates.insert(state).second)return;
    AnimationNotification event;event.LayerIndex=layerIndex;event.LayerName=definition.Name;event.Weight=layer.Weight;event.Type=AnimationNotificationType::StateEnter;event.State=definition.States[state].Name;
    event.Name=event.State;event.Clip=definition.States[state].Clip;
    event.SceneID=_GameObject->GetScene()->GetInstanceID();event.ObjectID=_GameObject->GetInstanceID();
    event.Time=float(state==layer.Current?layer.Time:layer.NextTime);
    if(event.Time==0)layer.StartMarkers.insert(state);
    if(!Owner().QueueAnimation(*this,event))MotionFailure("Animation notification queue exceeded 8192 pending records");
}
void AnimatorComponent::ExitState(std::uint32_t layerIndex,int state) {
    auto& layer=_Layers[layerIndex];if(!_ControllerData||_Generation!=Owner().GetAnimationResources().GetCachedGeneration()){layer.EnteredStates.clear();return;}
    const auto& definition=_ControllerData->Layers[layerIndex];
    if(!layer.EnteredStates.erase(state))return;
    layer.StartMarkers.erase(state);
    AnimationNotification event;event.LayerIndex=layerIndex;event.LayerName=definition.Name;event.Weight=layer.Weight;event.Type=AnimationNotificationType::StateExit;event.State=definition.States[state].Name;
    event.Name=event.State;event.Clip=definition.States[state].Clip;
    event.SceneID=_GameObject->GetScene()->GetInstanceID();event.ObjectID=_GameObject->GetInstanceID();
    event.Time=float(state==layer.Current?layer.Time:layer.NextTime);
    if(!Owner().QueueAnimation(*this,event))MotionFailure("Animation notification queue exceeded 8192 pending records");
}
void AnimatorComponent::ExitStates(std::uint32_t layerIndex){
    auto& layer=_Layers[layerIndex];const auto states=layer.EnteredStates;for(int state:states)ExitState(layerIndex,state);}
bool AnimatorComponent::StateMotions(std::uint32_t layerIndex,int stateIndex,double previous,double current,float beginWeight,float endWeight,std::vector<ActiveMotion>& output) {
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    if((beginWeight<=0&&endWeight<=0)||stateIndex<0)return true;
    const auto& state=definition.States[stateIndex];
    if(state.BlendParameter.empty()){const auto* clip=Owner().GetAnimationResources().Clip(state.Clip,_Error);const double duration=Duration(layerIndex,stateIndex);if(!clip||duration<0)return false;const double factor=duration>0?clip->Duration/duration:0;output.push_back({state.Clip,previous*factor,current*factor,beginWeight,endWeight,stateIndex});return true;}
    const double parameter=_Parameters[state.BlendParameter];
    const auto upper=std::upper_bound(state.Motions.begin(),state.Motions.end(),parameter,[](double p,const BlendMotion& motion){return p<motion.Threshold;});
    std::size_t right=upper==state.Motions.end()?state.Motions.size()-1:std::size_t(upper-state.Motions.begin());
    const std::size_t left=upper==state.Motions.end()?right:(right?right-1:0);
    const float weight=left==right?0:std::clamp(float((parameter-state.Motions[left].Threshold)/(state.Motions[right].Threshold-state.Motions[left].Threshold)),0.f,1.f);
    const double duration=Duration(layerIndex,stateIndex);if(duration<0)return false;
    for(const auto entry:std::vector<std::pair<std::size_t,float>>{{left,1-weight},{right,left==right?0:weight}})if(entry.second>0){
        const auto* clip=Owner().GetAnimationResources().Clip(state.Motions[entry.first].Clip,_Error);if(!clip)return false;
        const double factor=duration>0?clip->Duration/duration:0;
        output.push_back({state.Motions[entry.first].Clip,previous*factor,current*factor,beginWeight*entry.second,endWeight*entry.second,stateIndex});
    }return true;
}
bool AnimatorComponent::EvaluateMotionSignals(std::uint32_t layerIndex,const std::vector<ActiveMotion>& motions) {
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    struct Timed {double Fraction;AnimationNotification Event;};std::vector<Timed> events;
    RootMotionDelta blended;float totalWeight=0;
    for(const auto& motion:motions) {
        const auto* clip=Owner().GetAnimationResources().Clip(motion.Clip,_Error);if(!clip)return false;
        const bool loop=definition.States[motion.State].Loop&&_Loop;
        const float motionWeight=(motion.BeginWeight+motion.EndWeight)*.5f;
        if(_RootMotionMode!=RootMotionMode::Disabled){
            layer.HasRootMotion=layer.HasRootMotion||!clip->RootMotion.empty();
            RootMotionDelta delta;if(!AnimationMotion::Delta(*clip,motion.Previous,motion.Current,loop,delta,_Error))return false;
            if(!RetargetRoot(*clip,delta))return false;
            if(totalWeight==0)blended=delta;
            else blended=AnimationMotion::Blend(blended,delta,motionWeight/(totalWeight+motionWeight));
            totalWeight+=motionWeight;
        }
        if(layer.Weight==0||motion.Current<=motion.Previous||clip->Events.empty())continue;
        const bool initial=layer.StartMarkers.count(motion.State)&&motion.Previous==0&&motion.Current>0;
        for(const auto& marker:clip->Events) {
            double first=0,last=0;
            if(loop&&clip->Duration>0){first=initial&&marker.Time==0?0:std::max(0.0,std::floor((motion.Previous-marker.Time)/clip->Duration)+1);last=std::floor((motion.Current-marker.Time)/clip->Duration);}
            else if(!(motion.Previous<marker.Time&&motion.Current>=marker.Time)&&!(initial&&marker.Time==0))continue;
            if(last<first)continue;
            if(last>=double(std::numeric_limits<std::uint64_t>::max())||last-first+1>4096-_MotionEventCount-events.size()) {
                _Error="Animation event burst exceeds 4096 markers per Animator Update; reduce the time step";return false;
            }
            const auto startCycle=static_cast<std::uint64_t>(first);
            const auto count=static_cast<std::uint64_t>(last-first)+1;
            for(std::uint64_t occurrence=0;occurrence<count;++occurrence){
                const auto cycle=startCycle+occurrence;
                const double at=marker.Time+(loop?cycle*clip->Duration:0);
                AnimationNotification event;event.LayerIndex=layerIndex;event.LayerName=definition.Name;event.Weight=layer.Weight;event.Type=AnimationNotificationType::ClipEvent;
                event.SceneID=_GameObject->GetScene()->GetInstanceID();event.ObjectID=_GameObject->GetInstanceID();
                event.State=definition.States[motion.State].Name;event.Clip=motion.Clip;event.Name=marker.Name;event.Payload=marker.Payload;
                event.Time=marker.Time;
                const double fraction=(at-motion.Previous)/(motion.Current-motion.Previous);
                event.Weight=std::clamp(motion.BeginWeight+(motion.EndWeight-motion.BeginWeight)*float(fraction),0.f,1.f);
                if(initial&&marker.Time==0&&cycle==0)event.Weight=std::max(motion.BeginWeight,motion.EndWeight);
                event.Weight*=layer.Weight;
                if(event.Weight<=0)continue;
                event.LoopIndex=static_cast<std::uint64_t>(cycle);
                events.push_back({fraction,std::move(event)});
            }
        }
    }
    // A frozen interrupted source contributes identity velocity rather than renormalizing away its blend weight.
    if(totalWeight>0&&totalWeight<1)blended=AnimationMotion::Blend({},blended,totalWeight);
    std::stable_sort(events.begin(),events.end(),[](const Timed& a,const Timed& b){
        if(a.Fraction!=b.Fraction)return a.Fraction<b.Fraction;
        if(a.Event.Clip.Guid!=b.Event.Clip.Guid)return a.Event.Clip.Guid<b.Event.Clip.Guid;
        if(a.Event.Clip.LocalID!=b.Event.Clip.LocalID)return a.Event.Clip.LocalID<b.Event.Clip.LocalID;
        return a.Event.LoopIndex<b.Event.LoopIndex;
    });
    for(const auto& event:events)if(!Owner().QueueAnimation(*this,event.Event)){_Error="Animation notification queue exceeded 8192 pending records";return false;}
    for(const auto& motion:motions)if(motion.Current>motion.Previous)layer.StartMarkers.erase(motion.State);
    _MotionEventCount+=events.size();
    if(_RootMotionMode!=RootMotionMode::Disabled)layer.MotionDelta=AnimationMotion::Compose(layer.MotionDelta,blended);
    return true;
}
}
