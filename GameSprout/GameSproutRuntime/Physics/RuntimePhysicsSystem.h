#pragma once
#include "GPhysics.h"
#include "GameSproutRuntime.h"
#include "PhysicsComponents.h"
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace GameSproutRuntime {
class RuntimePhysicsSystem {
  public:
    explicit RuntimePhysicsSystem(Runtime &runtime) : _Runtime(runtime) {}
    ~RuntimePhysicsSystem();
    bool Prepare(std::string &error);
    bool Step(float deltaSeconds, std::string &error);
    void Reset();
    void InvalidateScene(uint64_t scene);
    void InvalidateTransform(uint64_t scene, uint64_t object);
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
        std::vector<uint64_t> DynamicObjects;
        std::vector<uint64_t> DynamicBodies;
        std::set<uint64_t> StaticTransforms;
        std::map<uint64_t, std::vector<uint64_t>> CollidersByObject;
        bool SnapshotKnown = false;
        bool BackendNeedsStep = false;
        uint64_t Revision = 0, SnapshotRevision = 0, ResourceGeneration = 0;
        IRuntimeResourceProvider* Provider=nullptr;
        std::array<std::uint32_t,32> CollisionMatrix{};
    };
    struct BodyRequest {
        IGameObject *Object = nullptr;
        RigidbodyComponent *Rigidbody = nullptr;
        GPhysics::BodySettings Description;
        Vector3 WorldScale{1, 1, 1};
        Mat4 World;
    };
    struct MeshShapeCache {
        ResourceReference Reference;
        Mat4 Relative;
        GPhysics::MeshGeometry Geometry;
        uint64_t Revision=0;
    };
    struct PreparedMeshSource {
        GPhysics::CookedMesh Mesh;
        uint64_t Revision=0;
        bool Attempted=false;
    };
    std::map<std::pair<std::string,uint64_t>,GPhysics::MeshGeometry> _MeshSources;
    std::map<std::pair<std::string,uint64_t>,std::array<PreparedMeshSource,2>> _PreparedMeshSources;
    std::map<uint64_t,MeshShapeCache> _MeshShapes;
    uint64_t _NextMeshRevision=0;
    bool AppendMeshCollider(BodyRequest &,MeshColliderComponent &,GPhysics::Shape &,const Mat4 &,std::string &);
    Runtime &_Runtime;
    GPhysics::MeshCookCache _CookedMeshes;
    std::map<uint64_t, SceneWorld *> _Worlds;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, uint64_t> _ColliderKeys;
    std::map<uint64_t, ColliderIdentity> _Identities;
    uint64_t _NextColliderKey = 0;
    IRuntimeResourceProvider *_Provider = nullptr;
    uint64_t _ProviderRevision = 0;
    uint64_t _ResourceGeneration = 1;
    std::map<std::pair<std::string,uint64_t>,uint64_t> _ResourceRevisions;
    std::map<std::pair<std::string, uint64_t>, PhysicsMaterialData> _Materials;
    void SyncResources();
    IComponent *ResolveComponent(uint64_t object, uint64_t component) const;
    PhysicsColliderState *ResolveCollider(const ColliderIdentity &) const;
    bool BuildRequests(IScene *, SceneWorld &, bool reuseStatic, std::map<uint64_t, BodyRequest> &, std::string &);
    bool Synchronize(IScene *, SceneWorld &, float, std::string &);
    bool AppendCollider(BodyRequest &, PhysicsColliderState &, std::string &);
    bool PublishTransforms(SceneWorld &, std::string &);
    void Dispatch(const std::vector<GPhysics::Contact> &);
    void ForgetMissingScenes();
};
} // namespace GameSproutRuntime
