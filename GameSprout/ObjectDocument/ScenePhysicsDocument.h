#pragma once
#include "SceneDocument.h"

namespace ScenePhysicsDocument {
bool Handles(const std::string& type);
bool IsCollider(const std::string& type);
void Validate(const Json::Value&,const std::string& path);
void Apply(const Json::Value&,GameSproutRuntime::IComponent&);
GameSproutRuntime::IComponent* Create(const Json::Value&,GameSproutRuntime::IGameObject&);
Json::Value Serialize(GameSproutRuntime::IComponent&);
}
