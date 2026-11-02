#pragma once
#include "RuntimePhysicsData.h"
#include "Math/TransformMath.h"

namespace GameSproutRuntime::RuntimePhysicsMath {
// The collider's complete affine matrix includes Center/Rotation. Scale remains
// separate so spheres stay spherical and capsules keep a circular cross-section.
struct ColliderFrame {
    Vector3 Position;
    Quaternion Rotation;
    Vector3 Scale{1,1,1};
};
inline bool TryGetColliderFrame(const Mat4& matrix,ColliderFrame& output) {
    ColliderFrame frame;
    if(!RuntimeMath::TryDecomposeTRS(matrix,frame.Position,frame.Rotation,frame.Scale)
        ||frame.Scale.X==0||frame.Scale.Y==0||frame.Scale.Z==0)return false;
    output=frame;return true;
}
inline Vector3 AbsoluteScale(Vector3 scale) {
    return {std::fabs(scale.X),std::fabs(scale.Y),std::fabs(scale.Z)};
}
inline float SphereScale(Vector3 scale) {
    scale=AbsoluteScale(scale);return std::max({scale.X,scale.Y,scale.Z});
}
inline float CapsuleHeightScale(Vector3 scale,PhysicsAxis axis) {
    scale=AbsoluteScale(scale);
    return axis==PhysicsAxis::X?scale.X:axis==PhysicsAxis::Y?scale.Y:scale.Z;
}
inline float CapsuleRadiusScale(Vector3 scale,PhysicsAxis axis) {
    scale=AbsoluteScale(scale);
    return axis==PhysicsAxis::X?std::max(scale.Y,scale.Z):
        axis==PhysicsAxis::Y?std::max(scale.X,scale.Z):std::max(scale.X,scale.Y);
}
}
