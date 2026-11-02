#pragma once
#include "GameSproutRuntimeApi.h"
#include "ISceneManager.h"

namespace PhysicsComponentValues {
enum class ShapeKind { Box,Sphere,Capsule,Mesh };
struct Collider {
    GameSproutRuntime::ColliderSettings Settings;
    ShapeKind Kind=ShapeKind::Box;
    GameSproutRuntime::BoxColliderShape Box;
    GameSproutRuntime::SphereColliderShape Sphere;
    GameSproutRuntime::CapsuleColliderShape Capsule;
    GameSproutRuntime::MeshColliderShape Mesh;
};
bool Read(const GameSproutRuntime::IColliderComponent&,Collider&);
bool Write(GameSproutRuntime::IColliderComponent&,const Collider&,std::string&);
bool Equal(const Collider&,const Collider&);
bool Equal(const GameSproutRuntime::RigidbodySettings&,const GameSproutRuntime::RigidbodySettings&);
GameSproutRuntime::IComponent* Resolve(GameSproutRuntime::IGameObject&,ISceneManager*,uint64_t fileID,uint64_t liveID);
}
