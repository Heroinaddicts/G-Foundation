#pragma once
#include "GameSproutRuntimeApi.h"
#include "RuntimeAnimationData.h"

namespace GameSproutRuntime {
class IAnimatorComponent : public IComponent {
public:
    // Target human profile, authored in the scene. Empty means Generic animation.
    virtual ResourceReference GetAvatar() const = 0;
    virtual void SetAvatar(const ResourceReference&) = 0;
    // All IK positions/hints/rotations are Animator-wrapper-local (model space).
    // Goals persist until Clear/Stop/configuration replacement; never dirty/save the scene.
    // Positive weights require a valid Human Avatar. Paused edits recompute the pose.
    // OnAnimatorIK executes only during Update, after all layers, before the solve.
    virtual bool SetIKGoal(AvatarIKGoal, const AnimatorIKGoal&) = 0;
    virtual AnimatorIKGoal GetIKGoal(AvatarIKGoal) const = 0;
    virtual bool SetLookAt(const AnimatorLookAt&) = 0;
    virtual AnimatorLookAt GetLookAt() const = 0;
    virtual void ClearIKGoals() = 0;
    virtual ResourceReference GetSkeleton() const = 0;
    virtual void SetSkeleton(const ResourceReference&) = 0;
    virtual ResourceReference GetController() const = 0;
    virtual void SetController(const ResourceReference&) = 0;
    virtual ResourceReference GetDefaultClip() const = 0;
    virtual void SetDefaultClip(const ResourceReference&) = 0;
    virtual const std::vector<AnimationNodeBinding>& GetNodeBindings() const = 0;
    virtual bool SetNodeBindings(const std::vector<AnimationNodeBinding>&) = 0;
    virtual float GetSpeed() const = 0;
    virtual bool SetSpeed(float) = 0;
    virtual bool GetPlayOnStart() const = 0;
    virtual void SetPlayOnStart(bool) = 0;
    virtual bool GetLoop() const = 0;
    virtual void SetLoop(bool) = 0;
    virtual RootMotionMode GetRootMotionMode() const = 0;
    virtual bool SetRootMotionMode(RootMotionMode) = 0;
    // During this object's OnRootMotion only: submit one collision-solved local delta.
    // Runtime applies it once after all listeners; submitting also suppresses the default delta.
    virtual bool ApplyRootMotionDelta(const RootMotionDelta&) = 0;
    virtual RootMotionDelta GetRootMotionDelta() const = 0; // Last evaluated Update; Seek never emits motion.
    // Runtime parameter changes and playback never mark the editing scene dirty.
    virtual bool SetFloat(const std::string&, float) = 0;
    virtual bool SetInt(const std::string&, std::int32_t) = 0;
    virtual bool SetBool(const std::string&, bool) = 0;
    virtual bool SetTrigger(const std::string&) = 0;
    virtual bool ResetTrigger(const std::string&) = 0;
    virtual bool GetParameter(const std::string&, double&) const = 0;
    virtual std::uint32_t GetLayerCount() const = 0;
    virtual std::string GetLayerName(std::uint32_t layer) const = 0;
    virtual float GetLayerWeight(std::uint32_t layer) const = 0;
    // Base layer is always Override, weight 1. Weights are transient instance state.
    virtual bool SetLayerWeight(std::uint32_t layer, float weight) = 0;
    virtual bool Play(const std::string& state = "", float normalizedTime = 0, std::uint32_t layer = 0) = 0;
    virtual bool CrossFade(const std::string&, float seconds, float normalizedTime = 0, std::uint32_t layer = 0) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0; // Clears evaluated poses; authoring transforms are retained.
    virtual bool Seek(float seconds, std::uint32_t layer = 0) = 0;
    virtual AnimatorPlaybackState GetPlaybackState(std::uint32_t layer = 0) const = 0;
    virtual const std::string& GetLastError() const = 0;
    virtual bool ResolveBone(const std::string& path, std::uint64_t& objectID) const = 0;
protected:
    using IComponent::IComponent;
};
class ISkinnedMeshRendererComponent : public IComponent {
public:
    // This component augments Mesh + MeshRenderer on the same object.
    virtual std::uint64_t GetAnimatorObjectID() const = 0;
    virtual bool SetAnimatorObjectID(std::uint64_t) = 0;
    // Borrowed palette order is Mesh bone order, not Skeleton or Clip order.
    virtual bool GetSkinMatrices(const std::vector<SkinBinding>& bones,
        std::vector<Mat4>& matrices, std::string& error) const = 0;
protected:
    using IComponent::IComponent;
};
}
