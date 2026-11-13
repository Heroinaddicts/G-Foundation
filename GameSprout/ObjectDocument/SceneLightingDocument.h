#pragma once
#include "SceneDocument.h"
#include <string_view>

namespace SceneLightingDocument {
bool Handles(std::string_view type);
void Validate(const Json::Value& component,const std::string& path);
GameSproutRuntime::IComponent* Create(const Json::Value& component,GameSproutRuntime::IGameObject& object);
Json::Value Serialize(GameSproutRuntime::IComponent& component);
}
