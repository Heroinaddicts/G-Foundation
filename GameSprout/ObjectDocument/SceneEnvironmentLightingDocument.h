#pragma once
#include "SceneDocument.h"

namespace SceneEnvironmentLightingDocument {
// Throws with a precise field path on invalid current data. The enclosing scene
// may omit EnvironmentLighting to request the normal, unlit None default.
GameSproutRuntime::EnvironmentLightingSettings Read(const Json::Value&,
    const std::string& path="Scene.EnvironmentLighting");
Json::Value Serialize(const GameSproutRuntime::EnvironmentLightingSettings&);
}
