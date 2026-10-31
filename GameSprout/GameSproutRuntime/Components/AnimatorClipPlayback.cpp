#include "AnimatorComponent.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "Animation/AnimationSampling.h"
#include "Animation/AnimationMotion.h"
#include "Animation/HumanRetarget.h"
#include "RuntimeMath.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace GameSproutRuntime {
namespace {
float ClipTime(double seconds,float duration,bool loop){
    if(duration<=0)return 0;
    return float(loop?std::fmod(seconds,double(duration)):std::min(seconds,double(duration)));
}
}

bool AnimatorComponent::ApplyClipPlayback(AnimationClipPlaybackState& playback){
    // Resolve only the Clip and its skeleton profiles. A missing/invalid Controller
    // must never prevent direct pose playback, and no borrowed resource is retained.
    auto& resources=Owner().GetAnimationResources();
    const auto* skeleton=resources.Skeleton(_Skeleton,_Error);if(!skeleton)return false;
    const auto* clip=resources.Clip(playback.Clip,_Error);if(!clip)return false;
    playback.Duration=clip->Duration;
    playback.Time=ClipTime(playback.Time,clip->Duration,playback.Loop);
    std::vector<TransformPose> poses;
    if(!clip->SourceAvatar.Guid.empty()){
        if(_Avatar.Guid.empty()){_Error="Humanoid clip requires a target Avatar";return false;}
        const auto* target=resources.Avatar(_Avatar,_Error);if(!target)return false;
        if(target->Skeleton!=_Skeleton){_Error="Target Avatar does not match Animator Skeleton";return false;}
        const auto* source=resources.Avatar(clip->SourceAvatar,_Error);if(!source)return false;
        const auto* sourceSkeleton=resources.Skeleton(source->Skeleton,_Error);if(!sourceSkeleton)return false;
        std::vector<float> coverage;
        if(!HumanRetarget::Sample(*clip,*sourceSkeleton,*source,*skeleton,*target,playback.Time,playback.Loop,true,poses,coverage,_Error))return false;
    }else{
        if(!AnimationSampling::Sample(*clip,*skeleton,playback.Time,playback.Loop,poses,_Error))return false;
        // Preview the pose in place. Extracted trajectories never move the model
        // wrapper or run a gameplay/root-motion callback.
        if(!AnimationMotion::Strip(*clip,*skeleton,playback.Time,playback.Loop,poses,_Error))return false;
    }
    std::vector<TransformComponent*> transforms;
    std::vector<std::uint64_t> applied;
    std::vector<Mat4> matrices;
    transforms.reserve(poses.size());applied.reserve(poses.size());matrices.reserve(poses.size());
    for(std::size_t i=0;i<poses.size();++i){
        std::uint64_t id=0;
        if(!ResolveBone(skeleton->Nodes[i].Path,id)){_Error="Animator skeleton node is missing: "+skeleton->Nodes[i].Path;return false;}
        auto* object=Owner().QueryGameObject(id);
        auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform());
        if(!transform||!transform->CanSetEvaluatedPose(_GameObject->GetInstanceID())){_Error="Skeleton pose is owned by another Animator";return false;}
        const auto matrix=RuntimeMath::ComposeTRS(poses[i].Position,poses[i].Rotation,poses[i].Scale);
        if(!RuntimeMath::IsFinite(matrix)){_Error="Clip pose matrix is not finite";return false;}
        transforms.push_back(transform);applied.push_back(id);matrices.push_back(matrix);
    }
    // Validation above is complete before mutating any bone, so a failed Clip
    // switch leaves the last valid pose and playback state intact.
    if(_Applied!=applied){
        const std::set<std::uint64_t> retained(applied.begin(),applied.end());
        for(auto id:_Applied)if(!retained.count(id)){
            auto* object=Owner().QueryGameObject(id);
            if(object)if(auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());
        }
    }
    if(std::find(applied.begin(),applied.end(),_GameObject->GetInstanceID())==applied.end())
        if(auto* transform=dynamic_cast<TransformComponent*>(_GameObject->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());
    for(std::size_t i=0;i<transforms.size();++i)transforms[i]->SetEvaluatedPose(_GameObject->GetInstanceID(),matrices[i]);
    _Applied=std::move(applied);_RootMotionDelta={};_Error.clear();return true;
}

bool AnimatorComponent::PlayClip(const ResourceReference& clip,float seconds,float speed,bool loop){
    if(clip.Guid.empty()||!clip.LocalID||!std::isfinite(seconds)||seconds<0||!std::isfinite(speed)||speed<0){_Error="Invalid direct Clip playback settings";return false;}
    AnimationClipPlaybackState next;next.Clip=clip;next.Active=true;next.Playing=true;next.Loop=loop;next.Time=seconds;next.Speed=speed;
    if(!ApplyClipPlayback(next))return false;
    ++_SignalVersion;_ClipPlayback=next;_Playing=false;_Stopped=true;return true;
}

bool AnimatorComponent::SetClipPlaybackPlaying(bool playing){
    if(!_ClipPlayback.Active){_Error="No direct Clip playback is active";return false;}
    auto next=_ClipPlayback;
    if(!ApplyClipPlayback(next))return false;
    next.Playing=playing;_ClipPlayback=next;++_SignalVersion;return true;
}

bool AnimatorComponent::SeekClip(float seconds){
    if(!_ClipPlayback.Active||!std::isfinite(seconds)||seconds<0){_Error="Invalid direct Clip seek time";return false;}
    auto next=_ClipPlayback;next.Time=seconds;
    if(!ApplyClipPlayback(next))return false;
    _ClipPlayback=next;++_SignalVersion;return true;
}

void AnimatorComponent::StopClip(){
    if(!_ClipPlayback.Active)return;
    _ClipPlayback={};_Playing=false;_Stopped=true;++_SignalVersion;
    // Keep the Controller's clock/parameters/transition snapshots intact. Only
    // remove evaluated transforms; its next explicit Play/Resume can evaluate it.
    for(auto id:_Applied){
        auto* object=Owner().QueryGameObject(id);
        if(object)if(auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());
    }
    if(auto* transform=dynamic_cast<TransformComponent*>(_GameObject->GetTransform()))transform->ClearEvaluatedPose(_GameObject->GetInstanceID());
    _Applied.clear();_RootMotionDelta={};_AccumulatedMotion={};_Error.clear();
}

void AnimatorComponent::UpdateClipPlayback(float delta){
    auto next=_ClipPlayback;
    if(next.Playing){
        const double seconds=double(next.Time)+double(delta)*next.Speed;
        next.Time=ClipTime(seconds,next.Duration,next.Loop);
        if(!next.Loop&&seconds>=next.Duration)next.Playing=false;
    }
    if(ApplyClipPlayback(next)){_ClipPlayback=next;return;}
    const std::string error=_Error;StopClip();_Error=error;
}
}
