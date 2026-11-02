#pragma once
#include "GameSproutRuntimeApi.h"
#include "RuntimePhysicsData.h"

namespace GameSproutRuntime {
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IRigidbodyComponent : public IComponent {
public:
    virtual const RigidbodySettings& GetSettings() const = 0;
    // Invalid values leave all settings unchanged. Disabling retains settings;
    // associated enabled colliders become static collision geometry.
    virtual bool SetSettings(const RigidbodySettings&) = 0;

    // Individual script-facing properties use the same atomic validation and
    // change notifications as SetSettings; unrelated current values are retained.
    bool IsEnabled() const { return GetSettings().Enabled; }
    bool SetEnabled(bool enabled) {
        auto settings=GetSettings();settings.Enabled=enabled;return SetSettings(settings);
    }
    PhysicsMotionType GetMotionType() const { return GetSettings().MotionType; }
    bool SetMotionType(PhysicsMotionType motionType) {
        auto settings=GetSettings();settings.MotionType=motionType;return SetSettings(settings);
    }
    float GetMass() const { return GetSettings().Mass; }
    bool SetMass(float mass) {
        auto settings=GetSettings();settings.Mass=mass;return SetSettings(settings);
    }
    float GetGravityFactor() const { return GetSettings().GravityFactor; }
    bool SetGravityFactor(float factor) {
        auto settings=GetSettings();settings.GravityFactor=factor;return SetSettings(settings);
    }
    float GetLinearDamping() const { return GetSettings().LinearDamping; }
    bool SetLinearDamping(float damping) {
        auto settings=GetSettings();settings.LinearDamping=damping;return SetSettings(settings);
    }
    float GetAngularDamping() const { return GetSettings().AngularDamping; }
    bool SetAngularDamping(float damping) {
        auto settings=GetSettings();settings.AngularDamping=damping;return SetSettings(settings);
    }
    bool IsContinuous() const { return GetSettings().Continuous; }
    bool SetContinuous(bool continuous) {
        auto settings=GetSettings();settings.Continuous=continuous;return SetSettings(settings);
    }
    std::uint32_t GetConstraints() const { return GetSettings().Constraints; }
    bool SetConstraints(std::uint32_t constraints) {
        auto settings=GetSettings();settings.Constraints=constraints;return SetSettings(settings);
    }
    bool SetConstraints(PhysicsConstraint constraints) {
        return SetConstraints(static_cast<std::uint32_t>(constraints));
    }
    // Freeze uses world axes. An invalid axis or an unsupported combination
    // returns false without changing any settings.
    // FreezeAll keeps collision/trigger participation; force and velocity
    // commands cannot accumulate motion while locked. Teleport remains explicit.
    bool IsPositionFrozen(PhysicsAxis axis) const {
        return axis<=PhysicsAxis::Z && (GetConstraints() & (1u<<static_cast<unsigned>(axis)))!=0;
    }
    bool SetPositionFrozen(PhysicsAxis axis,bool frozen) {
        if(axis>PhysicsAxis::Z)return false;
        const auto bit=1u<<static_cast<unsigned>(axis);
        return SetConstraints(frozen?GetConstraints()|bit:GetConstraints()&~bit);
    }
    bool IsRotationFrozen(PhysicsAxis axis) const {
        return axis<=PhysicsAxis::Z && (GetConstraints() & (8u<<static_cast<unsigned>(axis)))!=0;
    }
    bool SetRotationFrozen(PhysicsAxis axis,bool frozen) {
        if(axis>PhysicsAxis::Z)return false;
        const auto bit=8u<<static_cast<unsigned>(axis);
        return SetConstraints(frozen?GetConstraints()|bit:GetConstraints()&~bit);
    }
    virtual Vector3 GetLinearVelocity() const = 0;
    virtual bool SetLinearVelocity(Vector3) = 0; // World space, m/s.
    virtual Vector3 GetAngularVelocity() const = 0;
    virtual bool SetAngularVelocity(Vector3) = 0; // World space, rad/s.
    virtual bool AddForce(Vector3) = 0; // World space, N, next fixed step.
    virtual bool AddImpulse(Vector3) = 0; // World space, kg*m/s.
    virtual bool MoveKinematic(Vector3 position,Quaternion rotation) = 0;
    virtual bool Teleport(Vector3 position,Quaternion rotation,bool clearVelocity=true) = 0;
    virtual void WakeUp() = 0;
    virtual bool IsSleeping() const = 0;
protected:
    using IComponent::IComponent;
};
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IColliderComponent : public IComponent {
public:
    using fCollisionCallback=std::function<void(const CollisionEvent&)>;
    using fTriggerCallback=std::function<void(const TriggerEvent&)>;
    virtual const ColliderSettings& GetColliderSettings() const = 0;
    virtual bool SetColliderSettings(const ColliderSettings&) = 0;
    // Owner-thread CPU query; does not change properties, advance animation or
    // initialize a renderer. Uses only this object's prepared mesh, not children.
    virtual bool CalculateFit(ColliderFitResult&, std::string& error) const = 0;
    // Borrowed nearest ancestor/self rigidbody; nullptr denotes static geometry.
    virtual IRigidbodyComponent* GetAttachedRigidbody() const = 0;
    // Register on the Runtime owner thread; 0 means failure. Tokens are local
    // to this Collider and never reused. Unregistration immediately prevents
    // pending delivery, including from inside another callback.
    // Event values are borrowed for this call. No backend pointers are exposed.
    virtual std::uint64_t RegisterCollisionEnter(const fCollisionCallback&) = 0;
    virtual std::uint64_t RegisterCollisionStay(const fCollisionCallback&) = 0;
    virtual std::uint64_t RegisterCollisionExit(const fCollisionCallback&) = 0;
    virtual bool UnregisterCollisionEnter(std::uint64_t) = 0;
    virtual bool UnregisterCollisionStay(std::uint64_t) = 0;
    virtual bool UnregisterCollisionExit(std::uint64_t) = 0;
    virtual std::uint64_t RegisterTriggerEnter(const fTriggerCallback&) = 0;
    virtual std::uint64_t RegisterTriggerStay(const fTriggerCallback&) = 0;
    virtual std::uint64_t RegisterTriggerExit(const fTriggerCallback&) = 0;
    virtual bool UnregisterTriggerEnter(std::uint64_t) = 0;
    virtual bool UnregisterTriggerStay(std::uint64_t) = 0;
    virtual bool UnregisterTriggerExit(std::uint64_t) = 0;
protected:
    using IComponent::IComponent;
};
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IBoxColliderComponent : public IColliderComponent {
public:
    virtual BoxColliderShape GetShape() const = 0;
    virtual bool SetShape(const BoxColliderShape&) = 0;
protected:
    using IColliderComponent::IColliderComponent;
};
class GAMESPROUT_RUNTIME_PUBLIC_TYPE ISphereColliderComponent : public IColliderComponent {
public:
    virtual SphereColliderShape GetShape() const = 0;
    virtual bool SetShape(const SphereColliderShape&) = 0;
protected:
    using IColliderComponent::IColliderComponent;
};
class GAMESPROUT_RUNTIME_PUBLIC_TYPE ICapsuleColliderComponent : public IColliderComponent {
public:
    virtual CapsuleColliderShape GetShape() const = 0;
    virtual bool SetShape(const CapsuleColliderShape&) = 0;
protected:
    using IColliderComponent::IColliderComponent;
};
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IMeshColliderComponent : public IColliderComponent {
public:
    virtual MeshColliderShape GetShape() const = 0;
    // Reference syntax is validated here. Resource cooking and attached-body
    // restrictions are checked when Runtime prepares/updates physics.
    virtual bool SetShape(const MeshColliderShape&) = 0;
protected:
    using IColliderComponent::IColliderComponent;
};

}
