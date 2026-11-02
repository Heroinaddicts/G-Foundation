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
}
