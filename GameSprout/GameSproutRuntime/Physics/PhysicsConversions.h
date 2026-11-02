#pragma once
#include "Api/GameSproutRuntimeApi.h"
#include "Api/Math/TransformMath.h"
#include "GPhysics.h"
#include <cmath>

namespace GameSproutRuntime {
inline GPhysics::Vector3 PhysicsVector(Vector3 value) {
    return {value.X, value.Y, value.Z};
}
inline Vector3 RuntimeVector(GPhysics::Vector3 value) {
    return {value.X, value.Y, value.Z};
}
inline GPhysics::Quaternion PhysicsRotation(Quaternion value) {
    return {value.X, value.Y, value.Z, value.W};
}
inline Quaternion RuntimeRotation(GPhysics::Quaternion value) {
    return {value.X, value.Y, value.Z, value.W};
}
inline bool PhysicsNear(float a, float b) {
    return std::fabs(a - b) <= 0.00005f * std::max({1.0f, std::fabs(a), std::fabs(b)});
}
inline bool PhysicsNear(GPhysics::Vector3 a, GPhysics::Vector3 b) {
    return PhysicsNear(a.X, b.X) && PhysicsNear(a.Y, b.Y) && PhysicsNear(a.Z, b.Z);
}
inline bool PhysicsNear(GPhysics::Quaternion a, GPhysics::Quaternion b) {
    const float dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
    return std::fabs(std::fabs(dot) - 1) < 0.00005f;
}
inline bool PhysicsNear(const Mat4 &a, const Mat4 &b) {
    for (unsigned i = 0; i < 16; ++i)
        if (!PhysicsNear(a.Data[i], b.Data[i]))
            return false;
    return true;
}
inline bool PhysicsPose(const Mat4 &world, GPhysics::Pose &pose, Vector3 &scale, std::string &error) {
    Vector3 position;
    Quaternion rotation;
    if (!RuntimeMath::TryDecomposeTRS(world, position, rotation, scale) || scale.X <= 0 || scale.Y <= 0 ||
        scale.Z <= 0) {
        error = "Physics transforms require positive scale and an orthogonal TRS (no shear or zero scale)";
        return false;
    }
    pose = {PhysicsVector(position), PhysicsRotation(rotation)};
    return true;
}
inline bool PhysicsShapeEqual(const GPhysics::Shape &a, const GPhysics::Shape &b) {
    return a.Collider == b.Collider && a.Type == b.Type &&
           PhysicsNear(a.LocalPose.Position, b.LocalPose.Position) &&
           PhysicsNear(a.LocalPose.Rotation, b.LocalPose.Rotation) &&
           PhysicsNear(a.HalfExtents, b.HalfExtents) && PhysicsNear(a.Radius, b.Radius) &&
           PhysicsNear(a.HalfHeight, b.HalfHeight) && a.Trigger == b.Trigger && a.Layer == b.Layer &&
           a.Mask == b.Mask && a.Surface == b.Surface;
}
inline bool PhysicsBodyEqual(const GPhysics::BodySettings &a, const GPhysics::BodySettings &b) {
    if (a.Motion != b.Motion || a.Mass != b.Mass || a.GravityFactor != b.GravityFactor ||
        a.LinearDamping != b.LinearDamping || a.AngularDamping != b.AngularDamping ||
        a.AllowedDOFs != b.AllowedDOFs || a.Continuous != b.Continuous || a.Shapes.size() != b.Shapes.size())
        return false;
    for (std::size_t i = 0; i < a.Shapes.size(); ++i)
        if (!PhysicsShapeEqual(a.Shapes[i], b.Shapes[i]))
            return false;
    return true;
}
} // namespace GameSproutRuntime
