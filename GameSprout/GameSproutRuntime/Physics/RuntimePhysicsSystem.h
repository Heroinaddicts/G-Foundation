#pragma once
#include "GPhysics.h"
#include "GameSproutRuntime.h"
#include "PhysicsComponents.h"
#include <map>
#include <tuple>

namespace GameSproutRuntime {
class RuntimePhysicsSystem {
  public:
    explicit RuntimePhysicsSystem(Runtime &runtime) : _Runtime(runtime) {}
    ~RuntimePhysicsSystem();
    bool Prepare(std::string &error);
    bool Step(float deltaSeconds, std::string &error);
    void Reset();
    bool Raycast(IScene *, const Ray &, float, PhysicsRaycastHit &, const PhysicsQuerySettings &,
                 std::string &);

  private:
    struct BodyRecord {
        GPhysics::BodySettings Description;
        uint64_t ComponentID = 0, ConfigurationRevision = 0, VelocityRevision = 0, TransformRevision = 0;
        Vector3 WorldScale{1, 1, 1};
        Mat4 LastWorld;
        bool KinematicMoved = false;
    };
    struct SceneWorld {
        GPhysics::World Backend;
        std::map<uint64_t, BodyRecord> Bodies;
    };
    struct BodyRequest {
        IGameObject *Object = nullptr;
        RigidbodyComponent *Rigidbody = nullptr;
        GPhysics::BodySettings Description;
        Vector3 WorldScale{1, 1, 1};
        Mat4 World;
    };
    Runtime &_Runtime;
    std::map<uint64_t, SceneWorld *> _Worlds;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, uint64_t> _ColliderKeys;
    std::map<uint64_t, ColliderIdentity> _Identities;
    uint64_t _NextColliderKey = 0;
    IRuntimeResourceProvider *_Provider = nullptr;
    uint64_t _ProviderRevision = 0;
    std::map<std::pair<std::string, uint64_t>, PhysicsMaterialData> _Materials;
    IComponent *ResolveComponent(uint64_t object, uint64_t component) const;
    PhysicsColliderState *ResolveCollider(const ColliderIdentity &) const;
    bool BuildRequests(IScene *, std::map<uint64_t, BodyRequest> &, std::string &);
    bool Synchronize(IScene *, SceneWorld &, float, std::string &);
    bool AppendCollider(BodyRequest &, PhysicsColliderState &, std::string &);
    bool PublishTransforms(SceneWorld &, std::string &);
    void Dispatch(const std::vector<GPhysics::Contact> &);
    void ForgetMissingScenes();
};
} // namespace GameSproutRuntime
