#include "AnimatorComponent.h"
#include "Animation/AnimationSampling.h"
namespace GameSproutRuntime {
std::uint32_t AnimatorComponent::SourceLayer(std::uint32_t index)const{const int source=_ControllerData->Layers[index].SyncedLayerIndex;return source<0?index:std::uint32_t(source);}
double AnimatorComponent::Duration(std::uint32_t index,int state){
    const auto source=SourceLayer(index);double duration=RawDuration(source,state);if(duration<0)return -1;
    for(std::uint32_t layer=0;layer<_Layers.size();++layer)if(_ControllerData->Layers[layer].SyncedLayerIndex==int(source)&&_ControllerData->Layers[layer].SyncedLayerAffectsTiming){
        const double other=RawDuration(layer,state);if(other<0)return -1;duration=duration*(1-_Layers[layer].Weight)+other*_Layers[layer].Weight;
    }return duration;
}
void AnimatorComponent::SynchronizeLayers(std::uint32_t source){
    const auto& owner=_Layers[source];
    for(std::uint32_t index=0;index<_Layers.size();++index)if(_ControllerData->Layers[index].SyncedLayerIndex==int(source)){
        auto& layer=_Layers[index];const auto entered=layer.EnteredStates;
        for(int state:entered)if(state!=owner.Current&&state!=owner.Next)ExitState(index,state);
        layer.Current=owner.Current;layer.Next=owner.Next;layer.Time=owner.Time;layer.NextTime=owner.NextTime;
        layer.TransitionSeconds=owner.TransitionSeconds;layer.TransitionElapsed=owner.TransitionElapsed;
        if(owner.Frozen&&!layer.Frozen){layer.FrozenPose=layer.Pose;layer.FrozenCoverage=layer.PoseCoverage;layer.Frozen=!layer.FrozenPose.empty();}
        if(!owner.Frozen)layer.Frozen=false;
        EnterState(index,layer.Current);if(layer.Next>=0)EnterState(index,layer.Next);
    }
}
}
