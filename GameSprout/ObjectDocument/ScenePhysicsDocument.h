#pragma once
#include "SceneDocument.h"
#include <string_view>

namespace ScenePhysicsDocument {
bool Handles(std::string_view type);
bool IsCollider(std::string_view type);
void Validate(const Json::Value&,const std::string& path);
void Apply(const Json::Value&,GameSproutRuntime::IComponent&);
GameSproutRuntime::IComponent* Create(const Json::Value&,GameSproutRuntime::IGameObject&);
Json::Value Serialize(GameSproutRuntime::IComponent&);
}
