#pragma once
#include "Define.h"
#include "RuntimeHumanData.h"

// Authoring references remain path-based when an FBX changes node ordering.
struct HumanoidReferencePose {
    std::string Path;
    GameSproutRuntime::TransformPose Pose;
};
struct ModelHumanoidImportSettings {
    std::vector<GameSproutRuntime::HumanBoneMapping> HumanBones;
    // Empty means the imported Skeleton reference pose. Overrides identify stable paths.
    std::vector<HumanoidReferencePose> ReferencePose;
    float HumanScale=0; // Zero derives body scale from the validated reference pose.
};
struct ModelRigImportSettings : public ModelHumanoidImportSettings {
    eModelAnimationType AnimationType=eModelAnimationType::Generic;
};
