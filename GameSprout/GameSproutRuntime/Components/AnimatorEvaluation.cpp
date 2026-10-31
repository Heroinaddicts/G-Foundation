#include "AnimatorComponent.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "Animation/AnimationSampling.h"
#include "Animation/AnimationMotion.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>
namespace GameSproutRuntime {
namespace {
void BlendPair(const AnimatorState& state,double parameter,std::size_t& left,std::size_t& right,float& weight){
    const auto upper=std::upper_bound(state.Motions.begin(),state.Motions.end(),parameter,[](double p,const BlendMotion& motion){return p<motion.Threshold;});
    right=upper==state.Motions.end()?state.Motions.size()-1:std::size_t(upper-state.Motions.begin());left=right?right-1:0;
    if(upper==state.Motions.end())left=right;
    weight=left==right?0:std::clamp(float((parameter-state.Motions[left].Threshold)/(state.Motions[right].Threshold-state.Motions[left].Threshold)),0.f,1.f);
}
}
double AnimatorComponent::RawDuration(std::uint32_t layerIndex,int index){
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    const auto& state=definition.States[index];auto& resources=Owner().GetAnimationResources();
    if(state.BlendParameter.empty()){const auto* clip=resources.Clip(state.Clip,_Error);return clip?clip->Duration:-1;}
    std::size_t left,right;float weight;BlendPair(state,_Parameters[state.BlendParameter],left,right,weight);
    const auto* a=resources.Clip(state.Motions[left].Clip,_Error);if(!a)return -1;const auto* b=resources.Clip(state.Motions[right].Clip,_Error);if(!b)return -1;
    return (1-weight)*a->Duration/state.Motions[left].Speed+weight*b->Duration/state.Motions[right].Speed;
}
bool AnimatorComponent::StatePose(std::uint32_t layerIndex,int index,double time,std::vector<TransformPose>& output,const std::vector<TransformPose>* base,std::vector<float>* coverage){
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    const auto& state=definition.States[index];auto& resources=Owner().GetAnimationResources();
    auto sample=[&](const AnimationClipData& clip,double seconds,std::vector<TransformPose>& poses,std::vector<float>& writtenCoverage){
        if(!SampleClip(clip,seconds,state.Loop&&_Loop,poses,writtenCoverage))return false;
        if(base)for(std::size_t i=0;i<poses.size();++i)if(writtenCoverage[i]==0)poses[i]=(*base)[i];
        return true;
    };
    std::vector<float> writtenCoverage;
    if(state.BlendParameter.empty()){const auto* clip=resources.Clip(state.Clip,_Error);const double duration=Duration(layerIndex,index);if(!clip||duration<0||!sample(*clip,duration>0?time*clip->Duration/duration:0,output,writtenCoverage))return false;
        if(coverage)*coverage=std::move(writtenCoverage);return true;}
    std::size_t left,right;float weight;BlendPair(state,_Parameters[state.BlendParameter],left,right,weight);const double duration=Duration(layerIndex,index);if(duration<0)return false;
    const auto* a=resources.Clip(state.Motions[left].Clip,_Error);const auto* b=resources.Clip(state.Motions[right].Clip,_Error);if(!a||!b)return false;
    const double phase=duration>0?time/duration:0;std::vector<TransformPose> other;
    if(!sample(*a,phase*a->Duration,output,writtenCoverage))return false;
    if(left!=right){std::vector<float> otherCoverage;if(!sample(*b,phase*b->Duration,other,otherCoverage))return false;
        AnimationSampling::BlendContributions(output,writtenCoverage,other,otherCoverage,weight);}
    if(coverage)*coverage=std::move(writtenCoverage);return true;
}

bool AnimatorComponent::StartTransition(std::uint32_t layerIndex,int state,float seconds,float offset){
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    const double duration=Duration(layerIndex,state);if(duration<0)return false;
    std::vector<TransformPose> checked;if(!StatePose(layerIndex,state,duration*offset,checked))return false;
    if(seconds==0){ExitStates(layerIndex);layer.Current=state;layer.Time=duration*offset;layer.Next=-1;layer.Frozen=false;EnterState(layerIndex,layer.Current);return ApplyPose();}
    if(layer.Next>=0){for(std::uint32_t synced=0;synced<_Layers.size();++synced)if(_ControllerData->Layers[synced].SyncedLayerIndex==int(layerIndex)){auto& item=_Layers[synced];item.FrozenPose=item.Pose;item.FrozenCoverage=item.PoseCoverage;item.Frozen=!item.FrozenPose.empty();}
        ExitState(layerIndex,layer.Next);layer.FrozenPose=layer.Pose;layer.FrozenCoverage=layer.PoseCoverage;layer.Frozen=!layer.FrozenPose.empty();}
    layer.Next=state;layer.NextTime=duration*offset;layer.TransitionSeconds=seconds;layer.TransitionElapsed=0;EnterState(layerIndex,layer.Current);EnterState(layerIndex,layer.Next);return true;
}
bool AnimatorComponent::CrossFade(const std::string& name,float seconds,float offset,std::uint32_t layerIndex){if(!std::isfinite(seconds)||seconds<0||!std::isfinite(offset)||offset<0||!Ensure()||layerIndex>=_Layers.size())return false;StopClip();layerIndex=SourceLayer(layerIndex);const int state=StateIndex(layerIndex,name);if(state<0){_Error="Animator state does not exist: "+name;return false;}if(!StartTransition(layerIndex,state,seconds,offset))return false;SynchronizeLayers(layerIndex);_Playing=true;_Stopped=false;return true;}
bool AnimatorComponent::Conditions(const AnimatorTransition& transition)const{
    for(const auto& c:transition.Conditions){const auto p=_Parameters.find(c.Parameter);if(p==_Parameters.end())return false;const auto value=p->second;bool pass=false;switch(c.Compare){case AnimatorCompare::Greater:pass=value>c.Value;break;case AnimatorCompare::Less:pass=value<c.Value;break;case AnimatorCompare::Equal:pass=value==c.Value;break;case AnimatorCompare::NotEqual:pass=value!=c.Value;break;case AnimatorCompare::If:pass=value!=0;break;case AnimatorCompare::IfNot:pass=value==0;break;}if(!pass)return false;}return true;
}
bool AnimatorComponent::EvaluateTransitions(std::uint32_t layerIndex,double previous,double current){
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    if(layer.Next>=0)return true; // This format has no automatic transition-interruption policy.
    for(int pass=0;pass<2;++pass)for(const auto& transition:definition.Transitions){
        if((pass==0?transition.From!=-1:transition.From!=layer.Current)||(!transition.CanTransitionToSelf&&transition.To==std::uint32_t(layer.Current)))continue;
        if(transition.HasExitTime){bool crossed=false;const double exit=transition.ExitTime;
            if(exit<=1&&definition.States[layer.Current].Loop&&_Loop)crossed=std::floor(current-exit)>std::floor(previous-exit);
            else crossed=previous<exit&&current>=exit;
            if(!crossed)continue;
        }
        if(!Conditions(transition))continue;
        if(!StartTransition(layerIndex,int(transition.To),transition.Duration,transition.TargetOffset))return false;
        for(const auto& condition:transition.Conditions)for(const auto& parameter:_ControllerData->Parameters)if(parameter.Name==condition.Parameter&&parameter.Type==AnimatorParameterType::Trigger)_ConsumedTriggers.insert(parameter.Name);
        return true;
    }return true;
}
bool AnimatorComponent::UpdateLayer(std::uint32_t layerIndex,float delta){
    auto& layer=_Layers[layerIndex];const auto& definition=_ControllerData->Layers[layerIndex];
    layer.MotionDelta={};layer.HasRootMotion=false;
    EnterState(layerIndex,layer.Current);
    double remaining=delta;
    // Split an existing crossfade at its completion boundary: the old clip must not emit
    // markers or root velocity after it has exited, even during a long frame.
    do {
        const double duration=Duration(layerIndex,layer.Current);if(duration<0){MotionFailure(_Error);return false;}
        const double previousTime=layer.Time,previousNextTime=layer.NextTime;
        double step=remaining;
        if(layer.Next>=0&&_Speed>0)step=std::min(step,std::max(0.0,double(layer.TransitionSeconds-layer.TransitionElapsed)/_Speed));
        layer.Time+=step*_Speed*definition.States[layer.Current].Speed;
        std::vector<ActiveMotion> motions;
        if(layer.Next>=0){
            const float weightBefore=layer.TransitionSeconds>0?std::clamp(layer.TransitionElapsed/layer.TransitionSeconds,0.f,1.f):1;
            layer.NextTime+=step*_Speed*definition.States[layer.Next].Speed;layer.TransitionElapsed+=float(step*_Speed);
            const float weightAfter=layer.TransitionSeconds>0?std::clamp(layer.TransitionElapsed/layer.TransitionSeconds,0.f,1.f):1;
                if((!layer.Frozen&&!StateMotions(layerIndex,layer.Current,previousTime,layer.Time,1-weightBefore,1-weightAfter,motions))||!StateMotions(layerIndex,layer.Next,previousNextTime,layer.NextTime,weightBefore,weightAfter,motions)) {MotionFailure(_Error);return false;}
            if(step>0&&_Speed>0&&!EvaluateMotionSignals(layerIndex,motions)){MotionFailure(_Error);return false;}
            for(std::uint32_t synced=0;synced<_Layers.size();++synced)if(_ControllerData->Layers[synced].SyncedLayerIndex==int(layerIndex)){
                auto& item=_Layers[synced];EnterState(synced,layer.Current);EnterState(synced,layer.Next);std::vector<ActiveMotion> shared;
                if((!layer.Frozen&&!StateMotions(synced,layer.Current,previousTime,layer.Time,1-weightBefore,1-weightAfter,shared))||!StateMotions(synced,layer.Next,previousNextTime,layer.NextTime,weightBefore,weightAfter,shared)
                    ||(step>0&&_Speed>0&&!EvaluateMotionSignals(synced,shared))){MotionFailure(_Error);return false;}
            }
            if(layer.TransitionElapsed>=layer.TransitionSeconds-1e-7f){ExitState(layerIndex,layer.Current);layer.Current=layer.Next;layer.Time=layer.NextTime;layer.Next=-1;layer.Frozen=false;}
        }else {
            if(!StateMotions(layerIndex,layer.Current,previousTime,layer.Time,1,1,motions)){MotionFailure(_Error);return false;}
            if(step>0&&_Speed>0&&!EvaluateMotionSignals(layerIndex,motions)){MotionFailure(_Error);return false;}
            for(std::uint32_t synced=0;synced<_Layers.size();++synced)if(_ControllerData->Layers[synced].SyncedLayerIndex==int(layerIndex)){
                EnterState(synced,layer.Current);std::vector<ActiveMotion> shared;if(!StateMotions(synced,layer.Current,previousTime,layer.Time,1,1,shared)||(step>0&&_Speed>0&&!EvaluateMotionSignals(synced,shared))){MotionFailure(_Error);return false;}
            }
            if(!EvaluateTransitions(layerIndex,duration>0?previousTime/duration:0,duration>0?layer.Time/duration:0)){MotionFailure(_Error);return false;}
        }
        SynchronizeLayers(layerIndex);
        remaining-=step;
        if(step==0&&layer.Next>=0)break;
    }while(remaining>1e-8);
    return true;
}
}
