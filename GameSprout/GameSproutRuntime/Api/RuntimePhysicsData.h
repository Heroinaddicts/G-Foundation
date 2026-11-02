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
};
struct PhysicsRaycastHit {
    ColliderIdentity Collider;
    Vector3 Position, Normal;
    float Distance=0;
};
}
