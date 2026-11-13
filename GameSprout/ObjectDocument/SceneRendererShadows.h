#pragma once
#include "GameSproutRuntimeApi.h"
#include "JsonCpp/json.hpp"
#include <string>

namespace SceneRendererShadows {
struct Settings {
    GameSproutRuntime::ShadowCastingMode Casting = GameSproutRuntime::ShadowCastingMode::On;
    bool Receive = true;
};

Settings Read(const Json::Value& component, const std::string& path);
void Serialize(Json::Value& component, GameSproutRuntime::ShadowCastingMode casting, bool receive);
}
