#include "AnimatorComponent.h"
#include "GameSproutRuntime.h"
#include "Animation/HumanRetarget.h"
#include "Animation/AnimationSampling.h"
#include "Animation/AnimationMotion.h"
#include <set>
namespace GameSproutRuntime {
bool AnimatorComponent::SampleClip(const AnimationClipData& clip,double time,bool loop,std::vector<TransformPose>& output,std::vector<float>& coverage){
    if(!clip.SourceAvatar.Guid.empty()){
        if(!_AvatarData){_Error="Humanoid clip requires a target Avatar";return false;}
        auto& resources=Owner().GetAnimationResources();const auto* avatar=resources.Avatar(clip.SourceAvatar,_Error);if(!avatar)return false;
        const auto* skeleton=resources.Skeleton(avatar->Skeleton,_Error);if(!skeleton)return false;
        return HumanRetarget::Sample(clip,*skeleton,*avatar,*_SkeletonData,*_AvatarData,time,loop,_RootMotionMode!=RootMotionMode::Disabled,output,coverage,_Error);
    }
    if(!AnimationSampling::Sample(clip,*_SkeletonData,time,loop,output,_Error))return false;
    if(_RootMotionMode!=RootMotionMode::Disabled&&!AnimationMotion::Strip(clip,*_SkeletonData,time,loop,output,_Error))return false;
    std::set<std::string> tracks(clip.Tracks.begin(),clip.Tracks.end());coverage.resize(output.size());for(std::size_t i=0;i<output.size();++i)coverage[i]=tracks.count(_SkeletonData->Nodes[i].Path)?1:0;
    return true;
}
float AnimatorComponent::RetargetScale(const AnimationClipData& clip){
    if(clip.SourceAvatar.Guid.empty())return 1;
    if(!_AvatarData){_Error="Humanoid root motion requires target Avatar";return -1;}
    const auto* source=Owner().GetAnimationResources().Avatar(clip.SourceAvatar,_Error);return source?_AvatarData->HumanScale/source->HumanScale:-1;
}
}

namespace GameSproutRuntime {
bool AnimatorComponent::RetargetRoot(const AnimationClipData& clip,RootMotionDelta& delta){
 if(clip.SourceAvatar.Guid.empty())return true;
 const float scale=RetargetScale(clip);if(scale<0)return false;
 const auto* source=Owner().GetAnimationResources().Avatar(clip.SourceAvatar,_Error);Quaternion a,b;
 for(const auto& mapping:source->Bones)if(mapping.Bone==HumanBone::Hips)a=mapping.Axis;
 for(const auto& mapping:_AvatarData->Bones)if(mapping.Bone==HumanBone::Hips)b=mapping.Axis;
 const auto frame=HumanMath::Product(b,HumanMath::Inverse(a));delta.Translation=HumanMath::Rotate(frame,delta.Translation)*scale;
 delta.Rotation=HumanMath::Product(HumanMath::Product(frame,delta.Rotation),HumanMath::Inverse(frame));return true;
}
}
