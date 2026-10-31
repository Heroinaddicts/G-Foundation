#pragma once
#include "Api/HumanoidValidation.h"
namespace GameSproutRuntime::HumanRetarget {
bool Sample(const AnimationClipData&,const SkeletonData&,const HumanoidAvatarData&,const SkeletonData&,const HumanoidAvatarData&,
    double,bool,bool,std::vector<TransformPose>&,std::vector<float>&,std::string&);
}
