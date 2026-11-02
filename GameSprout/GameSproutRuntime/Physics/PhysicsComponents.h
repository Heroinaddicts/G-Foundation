#pragma once
#include "Api/GameSproutRuntimeApi.h"
#include "Components/BuiltinComponent.h"
#include <array>
#include <map>

namespace GameSproutRuntime {
class Runtime;
// Runtime-private storage shared by the concrete collider interfaces.
class PhysicsColliderState {
  public:
    virtual ~PhysicsColliderState() = default;
    virtual IColliderComponent *ColliderInterface() = 0;
    ColliderSettings Settings;
    uint64_t Revision = 1;
    uint64_t NextSubscription = 0;
    std::array<std::map<uint64_t, IColliderComponent::fCollisionCallback>, 3> CollisionCallbacks;
    std::array<std::map<uint64_t, IColliderComponent::fTriggerCallback>, 3> TriggerCallbacks;
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> SubscriptionOwners;
    uint64_t RegisterCollision(unsigned, const IColliderComponent::fCollisionCallback &);
    uint64_t RegisterTrigger(unsigned, const IColliderComponent::fTriggerCallback &);
    bool UnregisterCollision(unsigned, uint64_t);
    bool UnregisterTrigger(unsigned, uint64_t);
    void DropSubscriptions(uint64_t objectID, uint64_t componentID);
    void Dispatch(unsigned, const CollisionEvent &);
    void Dispatch(unsigned, const TriggerEvent &);
    void ConfigurationChanged();
};
template <typename Interface>
class ColliderComponentBase : public BuiltinComponent<Interface>, public PhysicsColliderState {
  public:
    IColliderComponent *ColliderInterface() override {
        return this;
    }
    const ColliderSettings &GetColliderSettings() const override {
        return Settings;
    }
    bool SetColliderSettings(const ColliderSettings &) override;
    bool CalculateFit(ColliderFitResult &, std::string &error) const override;
    IRigidbodyComponent *GetAttachedRigidbody() const override;
    uint64_t RegisterCollisionEnter(const IColliderComponent::fCollisionCallback &f) override {
        return RegisterCollision(0, f);
    }
    uint64_t RegisterCollisionStay(const IColliderComponent::fCollisionCallback &f) override {
        return RegisterCollision(1, f);
    }
    uint64_t RegisterCollisionExit(const IColliderComponent::fCollisionCallback &f) override {
        return RegisterCollision(2, f);
    }
    bool UnregisterCollisionEnter(uint64_t id) override {
        return UnregisterCollision(0, id);
    }
    bool UnregisterCollisionStay(uint64_t id) override {
        return UnregisterCollision(1, id);
    }
    bool UnregisterCollisionExit(uint64_t id) override {
        return UnregisterCollision(2, id);
    }
    uint64_t RegisterTriggerEnter(const IColliderComponent::fTriggerCallback &f) override {
        return RegisterTrigger(0, f);
    }
    uint64_t RegisterTriggerStay(const IColliderComponent::fTriggerCallback &f) override {
        return RegisterTrigger(1, f);
    }
    uint64_t RegisterTriggerExit(const IColliderComponent::fTriggerCallback &f) override {
        return RegisterTrigger(2, f);
    }
    bool UnregisterTriggerEnter(uint64_t id) override {
        return UnregisterTrigger(0, id);
    }
    bool UnregisterTriggerStay(uint64_t id) override {
        return UnregisterTrigger(1, id);
    }
    bool UnregisterTriggerExit(uint64_t id) override {
        return UnregisterTrigger(2, id);
    }
    void OnRelease(IGameSproutRuntime *) override {
        for (auto &callbacks : CollisionCallbacks)
            callbacks.clear();
        for (auto &callbacks : TriggerCallbacks)
            callbacks.clear();
        SubscriptionOwners.clear();
    }

  protected:
    ColliderComponentBase(IGameObject *owner, const char *type) : BuiltinComponent<Interface>(owner, type) {}
};
class BoxColliderComponent final : public ColliderComponentBase<IBoxColliderComponent> {
  public:
    explicit BoxColliderComponent(IGameObject *owner)
        : ColliderComponentBase(owner, "GameSproutRuntime.BoxCollider") {}
    BoxColliderShape GetShape() const override {
        return Shape;
    }
    bool SetShape(const BoxColliderShape &) override;
    BoxColliderShape Shape;
};
class SphereColliderComponent final : public ColliderComponentBase<ISphereColliderComponent> {
  public:
    explicit SphereColliderComponent(IGameObject *owner)
        : ColliderComponentBase(owner, "GameSproutRuntime.SphereCollider") {}
    SphereColliderShape GetShape() const override {
        return Shape;
    }
    bool SetShape(const SphereColliderShape &) override;
    SphereColliderShape Shape;
};
class CapsuleColliderComponent final : public ColliderComponentBase<ICapsuleColliderComponent> {
  public:
    explicit CapsuleColliderComponent(IGameObject *owner)
        : ColliderComponentBase(owner, "GameSproutRuntime.CapsuleCollider") {}
    CapsuleColliderShape GetShape() const override {
        return Shape;
    }
    bool SetShape(const CapsuleColliderShape &) override;
    CapsuleColliderShape Shape;
};
class MeshColliderComponent final : public ColliderComponentBase<IMeshColliderComponent> {
  public:
    explicit MeshColliderComponent(IGameObject *owner)
        : ColliderComponentBase(owner, "GameSproutRuntime.MeshCollider") {}
    MeshColliderShape GetShape() const override { return Shape; }
    bool SetShape(const MeshColliderShape &) override;
    MeshColliderShape Shape;
};
class RigidbodyComponent final : public BuiltinComponent<IRigidbodyComponent> {
  public:
    explicit RigidbodyComponent(IGameObject *owner)
        : BuiltinComponent(owner, "GameSproutRuntime.Rigidbody") {}
    const RigidbodySettings &GetSettings() const override {
        return Settings;
    }
    bool SetSettings(const RigidbodySettings &) override;
    Vector3 GetLinearVelocity() const override {
        return LinearVelocity;
    }
    bool SetLinearVelocity(Vector3) override;
    Vector3 GetAngularVelocity() const override {
        return AngularVelocity;
    }
    bool SetAngularVelocity(Vector3) override;
    bool AddForce(Vector3) override;
    bool AddImpulse(Vector3) override;
    bool MoveKinematic(Vector3, Quaternion) override;
    bool Teleport(Vector3, Quaternion, bool) override;
    void WakeUp() override {
        WakeRequested = true;
    }
    bool IsSleeping() const override {
        return Sleeping;
    }
    RigidbodySettings Settings;
    uint64_t Revision = 1, VelocityRevision = 0;
    Vector3 LinearVelocity, AngularVelocity, Force, Impulse;
    bool HasKinematicTarget = false, HasTeleport = false, ClearTeleportVelocity = true, WakeRequested = false,
         Sleeping = false;
    Vector3 TargetPosition;
    Quaternion TargetRotation;
};
IComponent *CreatePhysicsComponent(const std::type_info &, IGameObject *);
bool CalculateColliderFit(const IColliderComponent &,ColliderFitResult &,std::string &error);
void InitializeColliderFit(PhysicsColliderState &);
} // namespace GameSproutRuntime
