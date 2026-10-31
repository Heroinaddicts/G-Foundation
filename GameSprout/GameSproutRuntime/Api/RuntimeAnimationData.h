#pragma once
#include "Math/Mat4.h"
#include "RuntimeHumanData.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "ResourceReference.h"
#include <string>
#include <vector>
#include <cstdint>

namespace GameSproutRuntime {
// Pure prepared CPU data. No Editor, source decoder, Qt or GPU ownership.
struct SkeletonNode {
    std::string Path;
    std::string Name;
    std::int32_t Parent = -1;
    TransformPose Transform;
};
struct SkeletonData { std::vector<SkeletonNode> Nodes; };
struct SkinBinding { std::string NodePath; Mat4 InverseBindMatrix; };
struct ModelNode : SkeletonNode {
    TransformPose GeometryTransform;
    ResourceReference Mesh;
    std::vector<ResourceReference> Materials;
};
struct ModelData {
    ResourceReference Avatar;
    std::vector<ModelNode> Nodes;
    ResourceReference Skeleton;
    std::vector<ResourceReference> Animations;
};
// Cumulative rigid motion in the imported model/wrapper space, relative to clip start.
struct RootMotionDelta { Vector3 Translation; Quaternion Rotation; };
struct AnimationEventData { float Time = 0; std::string Name; std::string Payload; };
enum class RootMotionMode : std::uint8_t { Disabled, ExtractOnly, ApplyToObject };
enum class AnimationNotificationType : std::uint8_t { ClipEvent, StateEnter, StateExit };
struct AnimationNotification {
    AnimationNotificationType Type = AnimationNotificationType::ClipEvent;
    std::uint64_t SceneID = 0, ObjectID = 0;
    std::string State;
    std::uint32_t LayerIndex = 0;
    std::string LayerName;
    ResourceReference Clip;
    std::string Name, Payload;
    float Time = 0, Weight = 1;
    std::uint64_t LoopIndex = 0;
};
struct RootMotionNotification {
    std::uint64_t SceneID = 0, ObjectID = 0;
    RootMotionDelta Delta; // Local character-space delta; compose accumulated = accumulated * delta.
    RootMotionMode Mode = RootMotionMode::Disabled;
};
struct AnimationFrame { float Time = 0; std::vector<TransformPose> Poses; };
struct AnimationClipData {
    ResourceReference SourceAvatar; // Empty: Generic path-based clip; otherwise a prepared source human profile.
    float Duration = 0;
    float SampleRate = 30;
    std::vector<std::string> Tracks;
    std::vector<AnimationFrame> Frames;
    // Empty means no extracted trajectory. Otherwise one sample per Frames entry, first is identity.
    std::string RootMotionNodePath;
    std::vector<RootMotionDelta> RootMotion;
    std::vector<AnimationEventData> Events;
};
enum class AnimatorParameterType : std::uint8_t { Float, Int, Bool, Trigger };
struct AnimatorParameter { std::string Name; AnimatorParameterType Type = AnimatorParameterType::Float; double DefaultValue = 0; };
enum class AnimatorCompare : std::uint8_t { Greater, Less, Equal, NotEqual, If, IfNot };
struct AnimatorCondition { std::string Parameter; AnimatorCompare Compare = AnimatorCompare::Greater; double Value = 0; };
struct BlendMotion { ResourceReference Clip; float Threshold = 0; float Speed = 1; };
struct AnimatorState {
    std::string Name;
    ResourceReference Clip;
    float Speed = 1;
    bool Loop = true;
    // Empty: ordinary Clip; nonempty: 1D blend using sorted thresholds.
    std::string BlendParameter;
    std::vector<BlendMotion> Motions;
};
struct AnimatorTransition {
    std::int32_t From = -1; // -1 = AnyState.
    std::uint32_t To = 0;
    std::vector<AnimatorCondition> Conditions;
    bool HasExitTime = false;
    float ExitTime = 1;
    float Duration = 0.15f; // seconds.
    float TargetOffset = 0; // normalized destination time.
    bool CanTransitionToSelf = false;
};
enum class AnimatorLayerBlendingMode : std::uint8_t { Override, Additive };
struct AvatarMaskNode { std::string Path; float Weight = 1; };
enum class AvatarMaskMode : std::uint8_t { Transform, Humanoid };
struct AvatarMaskData {
    AvatarMaskMode Mode = AvatarMaskMode::Transform;
    std::vector<HumanBodyPartWeight> HumanParts;
    ResourceReference Skeleton;
    float DefaultWeight = 0;
    float RootMotionWeight = 0;
    // Sparse overrides inherit through SkeletonNode.Parent, never by display name.
    std::vector<AvatarMaskNode> Nodes;
};
struct AnimatorMotionOverride {
    std::string State;
    ResourceReference Clip;
    std::string BlendParameter;
    std::vector<BlendMotion> Motions;
};
struct AnimatorLayer {
    std::int32_t SyncedLayerIndex = -1;
    bool SyncedLayerAffectsTiming = false;
    bool IKPass = false;
    std::vector<AnimatorMotionOverride> MotionOverrides;
    std::string Name;
    AnimatorLayerBlendingMode BlendingMode = AnimatorLayerBlendingMode::Override;
    float DefaultWeight = 1;
    ResourceReference Mask;
    // Additive only: empty reference means skeleton rest; time is seconds, clamped, not looping.
    ResourceReference ReferenceClip;
    float ReferenceTime = 0;
    std::vector<AnimatorState> States;
    std::vector<AnimatorTransition> Transitions;
    std::uint32_t DefaultState = 0;
};
struct AnimatorControllerData {
    std::vector<AnimatorParameter> Parameters;
    std::vector<AnimatorLayer> Layers;
};
struct AnimationNodeBinding { std::string Path; std::uint64_t ObjectID = 0; };
struct AnimatorPlaybackState {
    // Synchronized layers report shared group logical seconds and identical normalized phase.
    std::string CurrentState;
    std::string NextState;
    float Time = 0;
    float NormalizedTime = 0;
    float TransitionWeight = 0;
    bool Playing = false;
};
}
