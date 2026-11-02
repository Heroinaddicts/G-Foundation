#pragma once
#include "ResourceReference.h"

namespace GameSproutRuntime {
enum class PrimitiveType : std::uint8_t { Cube, Sphere, Capsule, Cylinder, Plane, Quad };

// Reserved SDK namespace. Project GuidCreator always sets the UUID variant bits,
// so it never produces this identity. LocalIDs are stable scene/Prefab values.
inline constexpr const char* BuiltinMeshGuid = "00000000000000000000000000000001";
inline bool IsBuiltinResource(const ResourceReference& reference) {return reference.Guid==BuiltinMeshGuid;}
inline const char* PrimitiveName(PrimitiveType type) {
    switch(type) {
    case PrimitiveType::Cube:return "Cube";
    case PrimitiveType::Sphere:return "Sphere";
    case PrimitiveType::Capsule:return "Capsule";
    case PrimitiveType::Cylinder:return "Cylinder";
    case PrimitiveType::Plane:return "Plane";
    case PrimitiveType::Quad:return "Quad";
    }
    return nullptr;
}
inline ResourceReference GetBuiltinMeshReference(PrimitiveType type) {
    return PrimitiveName(type)?ResourceReference{BuiltinMeshGuid,static_cast<std::uint64_t>(type)+1}:ResourceReference{};
}
inline const char* GetBuiltinMeshName(const ResourceReference& reference) {
    if(!IsBuiltinResource(reference)||reference.LocalID<1||reference.LocalID>6)return nullptr;
    return PrimitiveName(static_cast<PrimitiveType>(reference.LocalID-1));
}
}
