#include "GPhysics.h"
#include "MeshCooking.h"
#include "MeshShapeCache.h"
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace {
std::mutex JoltLifetimeMutex;
unsigned JoltOwners = 0;
void AcquireJolt() {
    std::lock_guard<std::mutex> guard(JoltLifetimeMutex);
    if (JoltOwners++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}
void ReleaseJolt() {
    std::lock_guard<std::mutex> guard(JoltLifetimeMutex);
    if (--JoltOwners == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}
JPH::Vec3 Vector(GPhysics::Vector3 v) {
    return {v.X, v.Y, v.Z};
}
JPH::Quat Rotation(GPhysics::Quaternion v) {
    return {v.X, v.Y, v.Z, v.W};
}
GPhysics::Vector3 Vector(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}
GPhysics::Quaternion Rotation(JPH::QuatArg v) {
    return {v.GetX(), v.GetY(), v.GetZ(), v.GetW()};
}
bool Finite(GPhysics::Vector3 v) {
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}
bool ValidPose(const GPhysics::Pose &pose) {
    const auto q = pose.Rotation;
    const double n = double(q.X) * q.X + double(q.Y) * q.Y + double(q.Z) * q.Z + double(q.W) * q.W;
    return Finite(pose.Position) && std::isfinite(n) && std::fabs(n - 1.0) < 0.001;
}
float Combine(float a, float b, GPhysics::CombineMode first, GPhysics::CombineMode second) {
    const auto mode = std::max(first, second);
    double value = 0;
    switch (mode) {
    case GPhysics::CombineMode::Average:
        value = (double(a) + b) * 0.5;
        break;
    case GPhysics::CombineMode::Minimum:
        value = std::min(a, b);
        break;
    case GPhysics::CombineMode::Multiply:
        value = double(a) * b;
        break;
    case GPhysics::CombineMode::Maximum:
        value = std::max(a, b);
        break;
    }
    return static_cast<float>(std::min(value, double(std::numeric_limits<float>::max())));
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return 2;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(layer == 0 ? 0 : 1);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "Static" : "Moving";
    }
#endif
};
class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        return object != 2 && (object != 0 || broad.GetValue() == 1);
    }
};
class PairFilter final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != 2 && b != 2 && (a != 0 || b != 0);
    }
};
} // namespace

namespace GPhysics::Detail {
void AcquireMeshBackend() { AcquireJolt(); }
void ReleaseMeshBackend() { ReleaseJolt(); }
}
namespace GPhysics {
Material CombineMaterials(const Material &a, const Material &b) {
    Material result;
    result.FrictionCombine = std::max(a.FrictionCombine, b.FrictionCombine);
    result.RestitutionCombine = std::max(a.RestitutionCombine, b.RestitutionCombine);
    result.Friction = Combine(a.Friction, b.Friction, a.FrictionCombine, b.FrictionCombine);
    result.Restitution = Combine(a.Restitution, b.Restitution, a.RestitutionCombine, b.RestitutionCombine);
    return result;
}
struct World::Impl final : public JPH::ContactListener {
    struct BodyRecord {
        JPH::BodyID ID;
        BodySettings Settings;
    };
    struct RawKey {
        uint32_t FirstBody = 0, SecondBody = 0, FirstShape = 0, SecondShape = 0;
        bool operator<(const RawKey &other) const {
            return std::tie(FirstBody, SecondBody, FirstShape, SecondShape) <
                   std::tie(other.FirstBody, other.SecondBody, other.FirstShape, other.SecondShape);
        }
    };
    struct RawEvent {
        RawKey Key;
        Contact Value;
        bool Removed = false;
    };
    BroadLayers Layers;
    BroadFilter Broad;
    PairFilter Pairs;
    JPH::PhysicsSystem Physics;
    JPH::TempAllocatorImpl Temporary{16 * 1024 * 1024};
    JPH::JobSystemThreadPool Jobs;
    Detail::MeshShapeCache MeshCache;
    std::map<uint64_t, BodyRecord> Bodies;
    std::map<uint64_t, Shape> Shapes;
    std::mutex EventMutex;
    std::vector<RawEvent> Pending;
    std::map<RawKey, Contact> RawContacts;
    std::map<std::pair<uint64_t, uint64_t>, Contact> Contacts;
    std::map<uint32_t, bool> BodyActivity;
    std::atomic<bool> Overflow{false};
    bool Initialized = false;
    Impl()
        : Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
               static_cast<int>(std::min(8u, std::max(1u, std::thread::hardware_concurrency()) - 1))) {
        Pending.reserve(65536);
        Physics.Init(32768, 0, 65536, 32768, Layers, Broad, Pairs);
        Physics.SetContactListener(this);
        Initialized = true;
    }
    static uint64_t Collider(const JPH::Body &body, JPH::SubShapeID id) {
        return body.GetShape()->GetSubShapeUserData(id);
    }
    const Shape *FindShape(const JPH::Body &body, JPH::SubShapeID id) const {
        // Shared single-mesh leaves deliberately carry no collider identity.
        // Resolve the owning body's current values, never another body's cached
        // userdata/material/layer/trigger settings. Compound leaves retain the
        // original subshape userdata lookup below.
        const auto owner=Bodies.find(body.GetUserData());
        if(owner!=Bodies.end()&&owner->second.Settings.Shapes.size()==1)
            return &owner->second.Settings.Shapes.front();
        const auto found = Shapes.find(Collider(body, id));
        return found == Shapes.end() ? nullptr : &found->second;
    }
    bool Allowed(const Shape *a, const Shape *b) const {
        return a && b && (a->Mask & (1u << b->Layer)) && (b->Mask & (1u << a->Layer));
    }
    bool SemanticDynamic(const JPH::Body &body) const {
        const auto found = Bodies.find(body.GetUserData());
        return found != Bodies.end() && found->second.Settings.Motion == MotionType::Dynamic;
    }
    JPH::ValidateResult OnContactValidate(const JPH::Body &a, const JPH::Body &b, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult &hit) override {
        const Shape *first = FindShape(a, hit.mSubShapeID1), *second = FindShape(b, hit.mSubShapeID2);
        if (!Allowed(first, second))
            return JPH::ValidateResult::RejectContact;
        if (!SemanticDynamic(a) && !SemanticDynamic(b) && !first->Trigger && !second->Trigger)
            return JPH::ValidateResult::RejectContact;
        return JPH::ValidateResult::AcceptContact;
    }
    void Capture(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold,
                 JPH::ContactSettings &settings) {
        const Shape *first = FindShape(a, manifold.mSubShapeID1),
                    *second = FindShape(b, manifold.mSubShapeID2);
        if (!first || !second)
            return;
        const auto material = CombineMaterials(first->Surface, second->Surface);
        settings.mCombinedFriction = material.Friction;
        settings.mCombinedRestitution = material.Restitution;
        settings.mIsSensor = first->Trigger || second->Trigger;
        RawEvent event;
        event.Key = {a.GetID().GetIndexAndSequenceNumber(), b.GetID().GetIndexAndSequenceNumber(),
                     manifold.mSubShapeID1.GetValue(), manifold.mSubShapeID2.GetValue()};
        auto &contact = event.Value;
        contact.FirstCollider = first->Collider;
        contact.SecondCollider = second->Collider;
        contact.Trigger = settings.mIsSensor;
        contact.Normal = Vector(manifold.mWorldSpaceNormal);
        contact.Penetration = manifold.mPenetrationDepth;
        contact.Points.reserve(manifold.mRelativeContactPointsOn1.size());
        for (JPH::uint i = 0; i < manifold.mRelativeContactPointsOn1.size(); ++i)
            contact.Points.push_back(Vector(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(i))));
        if (contact.FirstCollider > contact.SecondCollider) {
            std::swap(contact.FirstCollider, contact.SecondCollider);
            contact.Normal = {-contact.Normal.X, -contact.Normal.Y, -contact.Normal.Z};
        }
        std::lock_guard<std::mutex> guard(EventMutex);
        if (Pending.size() < Pending.capacity())
            Pending.push_back(std::move(event));
        else
            Overflow = true;
    }
    void OnContactAdded(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold,
                        JPH::ContactSettings &settings) override {
        Capture(a, b, manifold, settings);
    }
    void OnContactPersisted(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold,
                            JPH::ContactSettings &settings) override {
        Capture(a, b, manifold, settings);
    }
    void OnContactRemoved(const JPH::SubShapeIDPair &pair) override {
        RawEvent event;
        event.Removed = true;
        event.Key = {pair.GetBody1ID().GetIndexAndSequenceNumber(),
                     pair.GetBody2ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID1().GetValue(),
                     pair.GetSubShapeID2().GetValue()};
        std::lock_guard<std::mutex> guard(EventMutex);
        if (Pending.size() < Pending.capacity())
            Pending.push_back(std::move(event));
        else
            Overflow = true;
    }
    bool Dormant(const RawKey &key) const {
        const auto first = BodyActivity.find(key.FirstBody), second = BodyActivity.find(key.SecondBody);
        return first != BodyActivity.end() && second != BodyActivity.end() && !first->second &&
               !second->second;
    }
};

World::World() = default;
World::~World() {
    if (_Impl) {
        while (!_Impl->Bodies.empty())
            RemoveBody(_Impl->Bodies.begin()->first);
        delete _Impl;
        ReleaseJolt();
    }
}
bool World::Initialize(std::string &error) {
    error.clear();
    if (_Impl)
        return true;
    AcquireJolt();
    try {
        _Impl = new Impl();
        return true;
    } catch (...) {
        ReleaseJolt();
        error = "Cannot initialize Jolt Physics world";
        return false;
    }
}
bool World::PutBody(const BodySettings &settings, std::string &error) {
    error.clear();
    if (!_Impl || !settings.Key || !ValidPose(settings.Transform) || !std::isfinite(settings.Mass) ||
        settings.Mass <= 0 || !Finite(settings.LinearVelocity) || !Finite(settings.AngularVelocity) ||
        (settings.AllowedDOFs & ~0x3fu) || settings.Motion > MotionType::Kinematic ||
        !std::isfinite(settings.GravityFactor) || !std::isfinite(settings.LinearDamping) ||
        settings.LinearDamping < 0 || !std::isfinite(settings.AngularDamping) ||
        settings.AngularDamping < 0) {
        error = "Invalid physics body settings";
        return false;
    }
    JPH::StaticCompoundShapeSettings compound;
    std::set<uint64_t> keys;
    for (const Shape &shape : settings.Shapes) {
        if (!shape.Collider || !keys.insert(shape.Collider).second || !ValidPose(shape.LocalPose) ||
            shape.Layer > 31 || !std::isfinite(shape.Surface.Friction) || shape.Surface.Friction < 0 ||
            !std::isfinite(shape.Surface.Restitution) || shape.Surface.Restitution < 0 ||
            shape.Surface.Restitution > 1 || shape.Type > ShapeType::ConvexHull ||
            shape.Surface.FrictionCombine > CombineMode::Maximum ||
            shape.Surface.RestitutionCombine > CombineMode::Maximum) {
            error = "Invalid physics collider settings";
            return false;
        }
        JPH::Shape::ShapeResult result;
        if (shape.Type == ShapeType::Box) {
            if (!Finite(shape.HalfExtents) || shape.HalfExtents.X <= 0 || shape.HalfExtents.Y <= 0 ||
                shape.HalfExtents.Z <= 0) {
                error = "Box dimensions must be positive";
                return false;
            }
            JPH::BoxShapeSettings source(Vector(shape.HalfExtents),
                                         std::min({0.05f, shape.HalfExtents.X * 0.1f,
                                                   shape.HalfExtents.Y * 0.1f, shape.HalfExtents.Z * 0.1f}));
            source.mUserData = shape.Collider;
            result = source.Create();
        } else if (shape.Type == ShapeType::Sphere) {
            if (!std::isfinite(shape.Radius) || shape.Radius <= 0) {
                error = "Sphere radius must be positive";
                return false;
            }
            JPH::SphereShapeSettings source(shape.Radius);
            source.mUserData = shape.Collider;
            result = source.Create();
        } else if (shape.Type == ShapeType::Capsule) {
            if (!std::isfinite(shape.Radius) || !std::isfinite(shape.HalfHeight) || shape.Radius <= 0 ||
                shape.HalfHeight < 0) {
                error = "Capsule dimensions are invalid";
                return false;
            }
            if (shape.HalfHeight == 0) {
                JPH::SphereShapeSettings source(shape.Radius);
                source.mUserData = shape.Collider;
                result = source.Create();
            } else {
                JPH::CapsuleShapeSettings source(shape.HalfHeight, shape.Radius);
                source.mUserData = shape.Collider;
                result = source.Create();
            }
        }
        else {
            if (!shape.Mesh || !shape.GeometryRevision) {error="MeshCollider geometry is unavailable";return false;}
            if (shape.Type==ShapeType::TriangleMesh && (settings.Motion==MotionType::Dynamic || shape.Trigger)) {
                error="Non-convex MeshCollider requires a static/kinematic non-trigger body";return false;
            }
            result=_Impl->MeshCache.Cook(*shape.Mesh,shape.Type==ShapeType::ConvexHull,
                settings.Shapes.size()==1?0:shape.Collider);
        }
        if (result.HasError()) {
            error = result.GetError().c_str();
            return false;
        }
        compound.AddShape(Vector(shape.LocalPose.Position), Rotation(shape.LocalPose.Rotation), result.Get());
    }
    JPH::Shape::ShapeResult shape;
    if (settings.Shapes.empty()) {
        JPH::EmptyShapeSettings empty;
        shape = empty.Create();
    } else
        shape = compound.Create();
    if (shape.HasError()) {
        error = shape.GetError().c_str();
        return false;
    }
    // Jolt cannot create a dynamic body with zero inverse mass and inertia.
    // A fully locked body therefore uses an immovable kinematic carrier; the
    // public settings retain their original motion type for callback semantics.
    const bool frozen = settings.AllowedDOFs == 0;
    const JPH::EMotionType motion = settings.Motion == MotionType::Static      ? JPH::EMotionType::Static
                                    : frozen                               ? JPH::EMotionType::Kinematic
                                    : settings.Motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                                               : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings description(shape.Get(), JPH::RVec3(Vector(settings.Transform.Position)),
                                          Rotation(settings.Transform.Rotation), motion,
                                          settings.Shapes.empty()              ? 2
                                          : motion == JPH::EMotionType::Static ? 0
                                                                               : 1);
    description.mUserData = settings.Key;
    description.mLinearVelocity = frozen ? JPH::Vec3::sZero() : Vector(settings.LinearVelocity);
    description.mAngularVelocity = frozen ? JPH::Vec3::sZero() : Vector(settings.AngularVelocity);
    description.mGravityFactor = settings.GravityFactor;
    description.mLinearDamping = settings.LinearDamping;
    description.mAngularDamping = settings.AngularDamping;
    description.mMotionQuality =
        settings.Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    // Keep the locked carrier available for contacts with static geometry too.
    description.mAllowSleeping = !frozen && settings.AllowSleeping;
    description.mUseManifoldReduction = false;
    description.mCollideKinematicVsNonDynamic = true;
    description.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(frozen ? 0x3fu : settings.AllowedDOFs);
    if (settings.Shapes.empty() || std::any_of(settings.Shapes.begin(),settings.Shapes.end(),[](const Shape& item){return item.Type==ShapeType::TriangleMesh;})) {
        description.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        description.mMassPropertiesOverride.mMass = settings.Mass;
        description.mMassPropertiesOverride.mInertia =
            JPH::Mat44::sScale(JPH::Vec3::sReplicate(settings.Mass));
    } else {
        description.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        description.mMassPropertiesOverride.mMass = settings.Mass;
    }
    JPH::Body *body = _Impl->Physics.GetBodyInterface().CreateBody(description);
    if (!body) {
        error = "Jolt body capacity exceeded";
        return false;
    }
    RemoveBody(settings.Key);
    auto saved=settings;
    for(auto &item:saved.Shapes)item.Mesh=nullptr;
    _Impl->Bodies.emplace(settings.Key, Impl::BodyRecord{body->GetID(), std::move(saved)});
    for (auto item : settings.Shapes) {
        item.Mesh=nullptr;
        _Impl->Shapes[item.Collider] = std::move(item);
    }
    _Impl->Physics.GetBodyInterface().AddBody(body->GetID(), motion == JPH::EMotionType::Static
                                                                 ? JPH::EActivation::DontActivate
                                                                 : JPH::EActivation::Activate);
    return true;
}
void World::RemoveBody(uint64_t key) {
    if (!_Impl)
        return;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end())
        return;
    auto &api = _Impl->Physics.GetBodyInterface();
    api.RemoveBody(found->second.ID);
    api.DestroyBody(found->second.ID);
    for (const auto &shape : found->second.Settings.Shapes)
        _Impl->Shapes.erase(shape.Collider);
    _Impl->Bodies.erase(found);
}
MeshCookStatistics World::GetMeshCookStatistics() const {
    return _Impl?_Impl->MeshCache.GetStatistics():MeshCookStatistics{};
}
bool World::ContainsBody(uint64_t key) const {
    return _Impl && _Impl->Bodies.count(key);
}
bool World::GetBodyState(uint64_t key, BodyState &state) const {
    if (!_Impl)
        return false;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end())
        return false;
    auto &api = _Impl->Physics.GetBodyInterface();
    state.Transform.Position = Vector(JPH::Vec3(api.GetPosition(found->second.ID)));
    state.Transform.Rotation = Rotation(api.GetRotation(found->second.ID));
    state.LinearVelocity = Vector(api.GetLinearVelocity(found->second.ID));
    state.AngularVelocity = Vector(api.GetAngularVelocity(found->second.ID));
    state.Active = api.IsActive(found->second.ID);
    return true;
}
bool World::SetPose(uint64_t key, const Pose &value, bool kinematic, float deltaSeconds) {
    if (!_Impl || !ValidPose(value))
        return false;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end())
        return false;
    auto &api = _Impl->Physics.GetBodyInterface();
    if (kinematic && deltaSeconds > 0 && found->second.Settings.AllowedDOFs != 0)
        api.MoveKinematic(found->second.ID, JPH::RVec3(Vector(value.Position)), Rotation(value.Rotation),
                          deltaSeconds);
    else
        api.SetPositionAndRotation(found->second.ID, JPH::RVec3(Vector(value.Position)),
                                   Rotation(value.Rotation), JPH::EActivation::Activate);
    return true;
}
bool World::SetVelocity(uint64_t key, Vector3 linear, Vector3 angular) {
    if (!_Impl || !Finite(linear) || !Finite(angular))
        return false;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end() || found->second.Settings.Motion == MotionType::Static)
        return false;
    if (found->second.Settings.AllowedDOFs == 0) {
        linear = {};
        angular = {};
    }
    _Impl->Physics.GetBodyInterface().SetLinearAndAngularVelocity(found->second.ID, Vector(linear),
                                                                  Vector(angular));
    _Impl->Physics.GetBodyInterface().ActivateBody(found->second.ID);
    return true;
}
bool World::AddForce(uint64_t key, Vector3 value, bool impulse) {
    if (!_Impl || !Finite(value))
        return false;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end() || found->second.Settings.Motion != MotionType::Dynamic)
        return false;
    if (found->second.Settings.AllowedDOFs == 0)
        return true;
    if (value.X == 0 && value.Y == 0 && value.Z == 0)
        return true;
    auto &api = _Impl->Physics.GetBodyInterface();
    if (impulse)
        api.AddImpulse(found->second.ID, Vector(value));
    else
        api.AddForce(found->second.ID, Vector(value));
    return true;
}
bool World::AddTorque(uint64_t key, Vector3 value, bool impulse) {
    if (!_Impl || !Finite(value))
        return false;
    const auto found = _Impl->Bodies.find(key);
    if (found == _Impl->Bodies.end() || found->second.Settings.Motion != MotionType::Dynamic)
        return false;
    if (found->second.Settings.AllowedDOFs == 0)
        return true;
    if (value.X == 0 && value.Y == 0 && value.Z == 0)
        return true;
    auto &api = _Impl->Physics.GetBodyInterface();
    if (impulse)
        api.AddAngularImpulse(found->second.ID, Vector(value));
    else
        api.AddTorque(found->second.ID, Vector(value));
    return true;
}
void World::SetGravity(Vector3 gravity) {
    if (_Impl && Finite(gravity))
        _Impl->Physics.SetGravity(Vector(gravity));
}
bool World::Step(float delta, std::vector<Contact> &events, std::string &error) {
    error.clear();
    events.clear();
    if (!_Impl || !std::isfinite(delta) || delta <= 0) {
        error = "Invalid physics time step";
        return false;
    }
    _Impl->Overflow = false;
    const auto status = _Impl->Physics.Update(delta, 1, &_Impl->Temporary, &_Impl->Jobs);
    _Impl->BodyActivity.clear();
    for (const auto &[key, body] : _Impl->Bodies)
        _Impl->BodyActivity.emplace(body.ID.GetIndexAndSequenceNumber(),
                                    _Impl->Physics.GetBodyInterface().IsActive(body.ID));
    std::set<Impl::RawKey> observed;
    for (auto &event : _Impl->Pending) {
        if (event.Removed) {
            if (!_Impl->Dormant(event.Key))
                _Impl->RawContacts.erase(event.Key);
        } else {
            _Impl->RawContacts[event.Key] = std::move(event.Value);
            observed.insert(event.Key);
        }
    }
    _Impl->Pending.clear();
    std::map<std::pair<uint64_t, uint64_t>, Contact> contacts;
    std::set<std::pair<uint64_t, uint64_t>> changed;
    for (auto it = _Impl->RawContacts.begin(); it != _Impl->RawContacts.end();) {
        if (!observed.count(it->first) && !_Impl->Dormant(it->first)) {
            it = _Impl->RawContacts.erase(it);
            continue;
        }
        const auto pair = std::make_pair(it->second.FirstCollider, it->second.SecondCollider);
        contacts[pair] = it->second;
        if (observed.count(it->first))
            changed.insert(pair);
        ++it;
    }
    for (auto &[pair, contact] : contacts) {
        const auto old = _Impl->Contacts.find(pair);
        if (old != _Impl->Contacts.end() && old->second.Trigger != contact.Trigger) {
            auto exit = old->second;
            exit.State = Contact::Phase::Exit;
            events.push_back(std::move(exit));
        }
        contact.State = old == _Impl->Contacts.end() || old->second.Trigger != contact.Trigger
                            ? Contact::Phase::Enter
                            : Contact::Phase::Stay;
        if (contact.State == Contact::Phase::Enter || changed.count(pair))
            events.push_back(contact);
    }
    for (const auto &[pair, contact] : _Impl->Contacts)
        if (!contacts.count(pair)) {
            auto exit = contact;
            exit.State = Contact::Phase::Exit;
            events.push_back(std::move(exit));
        }
    _Impl->Contacts = std::move(contacts);
    if (status != JPH::EPhysicsUpdateError::None || _Impl->Overflow) {
        error = "Jolt physics step exceeded configured contact/body capacity";
        return false;
    }
    return true;
}
bool World::Raycast(Vector3 origin, Vector3 displacement, uint32_t mask, bool triggers, RayHit &hit,
                    const std::vector<uint64_t>& ignoredColliders) const {
    hit = {};
    if (!_Impl || !Finite(origin) || !Finite(displacement))
        return false;
    const std::set<uint64_t> ignored(ignoredColliders.begin(),ignoredColliders.end());
    class RayShapeFilter final : public JPH::ShapeFilter {
      public:
        RayShapeFilter(const std::map<uint64_t,Shape>& shapes,const std::set<uint64_t>& ignored,uint32_t mask,bool triggers)
            : _Shapes(shapes),_Ignored(ignored),_Mask(mask),_Triggers(triggers) {}
        bool ShouldCollide(const JPH::Shape* shape,const JPH::SubShapeID&) const override {
            // Zero-userdata includes shared single-mesh leaves. Their owning
            // BodyRecord is filtered after the all-hit query, below.
            if(!shape->GetUserData())return true;
            const auto found=_Shapes.find(shape->GetUserData());
            // Compound/decorator roots carry no collider key. Let Jolt enter
            // those shapes, then filter each registered primitive leaf.
            if(found==_Shapes.end())return true;
            const auto& description=found->second;
            return !_Ignored.contains(description.Collider)&&(_Mask&(1u<<description.Layer))&&(_Triggers||!description.Trigger);
        }
      private:
        const std::map<uint64_t,Shape>& _Shapes;
        const std::set<uint64_t>& _Ignored;
        uint32_t _Mask;
        bool _Triggers;
    } filter(_Impl->Shapes,ignored,mask,triggers);
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    _Impl->Physics.GetNarrowPhaseQuery().CastRay(
        JPH::RRayCast(JPH::RVec3(Vector(origin)), Vector(displacement)), JPH::RayCastSettings{}, collector,
        {}, {}, {}, filter);
    float fraction = 1.0f;
    bool found = false;
    for (const auto &result : collector.mHits) {
        if (result.mFraction > fraction)
            continue;
        JPH::BodyLockRead lock(_Impl->Physics.GetBodyLockInterface(), result.mBodyID);
        if (!lock.Succeeded())
            continue;
        const auto &body = lock.GetBody();
        const Shape *shape = _Impl->FindShape(body, result.mSubShapeID2);
        if (!shape || ignored.contains(shape->Collider) || !(mask & (1u << shape->Layer)) || (!triggers && shape->Trigger))
            continue;
        fraction = result.mFraction;
        found = true;
        hit.Collider = shape->Collider;
        hit.Fraction = fraction;
        const JPH::RVec3 position = JPH::RVec3(Vector(origin)) + fraction * Vector(displacement);
        hit.Position = Vector(JPH::Vec3(position));
        hit.Normal = Vector(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, position));
    }
    return found;
}
} // namespace GPhysics
