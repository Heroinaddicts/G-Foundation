#pragma once
#include "Api/RuntimeAnimationData.h"
namespace GameSproutRuntime::AnimationSampling {
bool Validate(const SkeletonData&, std::string&);
bool Validate(const AnimationClipData&, std::string&);
bool Validate(const AnimatorControllerData&, std::string&);
TransformPose Blend(const TransformPose&, const TransformPose&, float weight);
// Mix only authored bone contributions. The remaining coverage belongs to the
// current lower/reference pose, which must never be baked into a frozen fade.
void BlendContributions(std::vector<TransformPose>& poses, std::vector<float>& coverage,
                        const std::vector<TransformPose>& other, const std::vector<float>& otherCoverage,
                        float weight);
bool Sample(const AnimationClipData&, const SkeletonData&, double seconds, bool loop,
            std::vector<TransformPose>& output, std::string& error);
}
