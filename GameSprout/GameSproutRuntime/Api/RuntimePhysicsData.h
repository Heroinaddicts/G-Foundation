#pragma once
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "ResourceReference.h"
#include "RuntimeProjectSettings.h"
#include <cstdint>
#include <vector>

namespace GameSproutRuntime {
enum class PhysicsMaterialCombine : std::uint8_t { Average, Minimum, Multiply, Maximum };
struct PhysicsMaterialData {
    float Friction = 0.6f;
    float Restitution = 0.0f;
    PhysicsMaterialCombine FrictionCombine = PhysicsMaterialCombine::Average;
    PhysicsMaterialCombine RestitutionCombine = PhysicsMaterialCombine::Average;
};
enum class PhysicsMotionType : std::uint8_t { Dynamic, Kinematic };
enum class PhysicsAxis : std::uint8_t { X, Y, Z };
enum class PhysicsConstraint : std::uint32_t {
    None=0,
    FreezePositionX=1u, FreezePositionY=2u, FreezePositionZ=4u,
    FreezeRotationX=8u, FreezeRotationY=16u, FreezeRotationZ=32u,
    FreezePosition=7u, FreezeRotation=56u, FreezeAll=63u
};
constexpr PhysicsConstraint operator|(PhysicsConstraint left,PhysicsConstraint right) {
    return static_cast<PhysicsConstraint>(static_cast<std::uint32_t>(left)|static_cast<std::uint32_t>(right));
}
struct RigidbodySettings {
    bool Enabled = true;
    PhysicsMotionType MotionType = PhysicsMotionType::Dynamic;
    float Mass = 1.0f; // kg, positive.
    float GravityFactor = 1.0f;
    float LinearDamping = 0.05f;
    float AngularDamping = 0.05f;
    std::uint32_t Constraints = 0; // PhysicsConstraint bits, world axes.
    bool Continuous = false;
};
struct ColliderSettings {
    bool Enabled = true;
    bool IsTrigger = false;
    Vector3 Center;
    Quaternion Rotation;
    ResourceReference Material; // Empty uses the Runtime default material.
};
struct BoxColliderShape { Vector3 Size{1,1,1}; }; // Full side lengths, meters.
struct SphereColliderShape { float Radius=0.5f; };
struct CapsuleColliderShape {
    float Radius=0.5f;
    float Height=2.0f; // Full height including both hemispheres; >= 2 * Radius.
    PhysicsAxis Axis=PhysicsAxis::Y;
};
// References the prepared Mesh subresource, not a source FBX or renderer instance.
// Non-convex meshes are static/kinematic triangle surfaces; Dynamic bodies and
// triggers require Convex. Empty Mesh means no collision geometry.
struct MeshColliderShape { ResourceReference Mesh; bool Convex=false; };
// Read-only estimate from the owning object's prepared, unskinned local mesh.
// HasBounds=false means there is no usable local mesh; default values remain
// available for creation, while an explicit editor Fit can leave authoring alone.
struct ColliderFitResult {
    bool HasBounds=false;
    Vector3 Center;
    BoxColliderShape Box;
    SphereColliderShape Sphere;
    CapsuleColliderShape Capsule;
    MeshColliderShape Mesh;
};
struct ColliderIdentity {
    std::uint64_t SceneID=0, ObjectID=0, ComponentID=0;
    bool operator==(const ColliderIdentity&) const = default;
};
struct PhysicsContact {
    Vector3 Position;
    Vector3 Normal; // World-space direction from Other towards Self.
    float Separation=0;
};
struct CollisionEvent {
    ColliderIdentity Self, Other;
    Vector3 RelativeVelocity;
    std::vector<PhysicsContact> Contacts;
};
struct TriggerEvent { ColliderIdentity Self, Other; };
struct PhysicsQuerySettings {
    std::uint32_t CollisionMask=DefaultRaycastLayers; // Explicit queries do not consult the collision matrix.
    bool IncludeTriggers=false;
    // Runtime GetInstanceID values in the queried scene, never pointers or
    // document FileIDs. Excludes every Collider on that object. A Rigidbody
    // owner also excludes all child Colliders attached to its compound body.
    // Independent descendant Rigidbody bodies are not excluded recursively.
    // Duplicate, missing and other-scene IDs have no effect.
    std::vector<std::uint64_t> IgnoredObjectIDs;
};
struct PhysicsRaycastHit {
    ColliderIdentity Collider;
    Vector3 Position, Normal;
    float Distance=0;
};
}
