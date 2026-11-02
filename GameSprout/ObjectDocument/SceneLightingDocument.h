#pragma once
#include "SceneDocument.h"

namespace SceneLightingDocument {
bool Handles(const std::string& type);
void Validate(const Json::Value& component,const std::string& path);
GameSproutRuntime::IComponent* Create(const Json::Value& component,GameSproutRuntime::IGameObject& object);
Json::Value Serialize(GameSproutRuntime::IComponent& component);
}
