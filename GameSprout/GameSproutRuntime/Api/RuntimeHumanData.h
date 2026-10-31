#pragma once
#include "RuntimePose.h"
#include "ResourceReference.h"
#include <string>
#include <vector>
#include <cstdint>
namespace GameSproutRuntime {
enum class HumanBone : std::uint8_t {
 Hips, LeftUpperLeg, RightUpperLeg, LeftLowerLeg, RightLowerLeg, LeftFoot, RightFoot,
 Spine, Chest, Neck, Head, LeftShoulder, RightShoulder, LeftUpperArm, RightUpperArm,
 LeftLowerArm, RightLowerArm, LeftHand, RightHand, LeftToes, RightToes, LeftEye, RightEye, Jaw,
 LeftThumbProximal, LeftThumbIntermediate, LeftThumbDistal,
 LeftIndexProximal, LeftIndexIntermediate, LeftIndexDistal,
 LeftMiddleProximal, LeftMiddleIntermediate, LeftMiddleDistal,
 LeftRingProximal, LeftRingIntermediate, LeftRingDistal,
 LeftLittleProximal, LeftLittleIntermediate, LeftLittleDistal,
 RightThumbProximal, RightThumbIntermediate, RightThumbDistal,
 RightIndexProximal, RightIndexIntermediate, RightIndexDistal,
 RightMiddleProximal, RightMiddleIntermediate, RightMiddleDistal,
 RightRingProximal, RightRingIntermediate, RightRingDistal,
 RightLittleProximal, RightLittleIntermediate, RightLittleDistal, UpperChest, Count
};
struct HumanBoneMapping {
 HumanBone Bone = HumanBone::Hips;
 std::string Path;
 // Canonical anatomical axes -> model space in the reference pose. Derived from geometry.
 Quaternion Axis;
};
struct HumanoidAvatarData {
 ResourceReference Skeleton;
 std::vector<HumanBoneMapping> Bones;
 std::vector<TransformPose> ReferencePose; // All Skeleton nodes, in Skeleton order; local TRS.
 float HumanScale = 1; // Reference hip-to-head plus longest hip-to-foot distance, metres.
};
enum class HumanBodyPart : std::uint8_t { Body, Head, LeftArm, RightArm, LeftLeg, RightLeg, LeftFingers, RightFingers, Count };
struct HumanBodyPartWeight { HumanBodyPart Part = HumanBodyPart::Body; float Weight = 1; };
inline const char* HumanBodyPartName(HumanBodyPart part){static const char* names[]={"Body","Head","LeftArm","RightArm","LeftLeg","RightLeg","LeftFingers","RightFingers"};return part<HumanBodyPart::Count?names[unsigned(part)]:"Invalid";}
inline HumanBodyPart HumanBoneBodyPart(HumanBone bone){
 if(bone>=HumanBone::LeftThumbProximal&&bone<=HumanBone::LeftLittleDistal)return HumanBodyPart::LeftFingers;
 if(bone>=HumanBone::RightThumbProximal&&bone<=HumanBone::RightLittleDistal)return HumanBodyPart::RightFingers;
 switch(bone){
 case HumanBone::LeftUpperLeg:case HumanBone::LeftLowerLeg:case HumanBone::LeftFoot:case HumanBone::LeftToes:return HumanBodyPart::LeftLeg;
 case HumanBone::RightUpperLeg:case HumanBone::RightLowerLeg:case HumanBone::RightFoot:case HumanBone::RightToes:return HumanBodyPart::RightLeg;
 case HumanBone::LeftShoulder:case HumanBone::LeftUpperArm:case HumanBone::LeftLowerArm:case HumanBone::LeftHand:return HumanBodyPart::LeftArm;
 case HumanBone::RightShoulder:case HumanBone::RightUpperArm:case HumanBone::RightLowerArm:case HumanBone::RightHand:return HumanBodyPart::RightArm;
 case HumanBone::Neck:case HumanBone::Head:case HumanBone::LeftEye:case HumanBone::RightEye:case HumanBone::Jaw:return HumanBodyPart::Head;
 default:return HumanBodyPart::Body;
 }
}
enum class AvatarIKGoal : std::uint8_t { LeftFoot, RightFoot, LeftHand, RightHand, Count };
struct AnimatorIKGoal {
 // Animator wrapper-local space, unaffected by its world position/rotation/scale.
 Vector3 Position; Quaternion Rotation; Vector3 Hint;
 float PositionWeight = 0, RotationWeight = 0, HintWeight = 0;
};
struct AnimatorLookAt {
 Vector3 Position;
 float Weight = 0, BodyWeight = .5f, HeadWeight = 1, EyesWeight = 0, ClampWeight = .5f;
};
struct AnimatorIKNotification { std::uint64_t SceneID=0,ObjectID=0; std::uint32_t LayerIndex=0; };
}
