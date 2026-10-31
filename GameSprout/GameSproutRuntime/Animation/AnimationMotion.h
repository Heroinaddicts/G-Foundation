#pragma once
#include "Api/RuntimeAnimationData.h"
namespace GameSproutRuntime::AnimationMotion {
RootMotionDelta Compose(const RootMotionDelta&,const RootMotionDelta&);
RootMotionDelta Inverse(const RootMotionDelta&);
RootMotionDelta Blend(const RootMotionDelta&,const RootMotionDelta&,float weight);
Mat4 Matrix(const RootMotionDelta&);
bool Sample(const AnimationClipData&,double time,bool loop,RootMotionDelta&,std::string& error);
bool Delta(const AnimationClipData&,double previous,double current,bool loop,RootMotionDelta&,std::string& error);
bool Strip(const AnimationClipData&,const SkeletonData&,double time,bool loop,std::vector<TransformPose>&,std::string& error);
}
