#pragma once
#include "SceneDocument.h"

// Shared authoring-data adaptation. Runtime owns binding and playback.
namespace SceneAnimationDocument {
bool Handles(const std::string& type);
void Validate(const Json::Value& data, const std::string& path);
void Create(const Json::Value& data, GameSproutRuntime::IGameObject& object);
Json::Value Serialize(GameSproutRuntime::IComponent& component,
    const SceneDocument::ObjectIDs& ids);
void ValidateReferences(const Json::Value& document);
void RestoreBindings(const Json::Value& snapshot, GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene& scene);
void Bind(const Json::Value& document, GameSproutRuntime::IScene& scene,
    const std::map<std::uint64_t, GameSproutRuntime::IGameObject*>& objects);
}
