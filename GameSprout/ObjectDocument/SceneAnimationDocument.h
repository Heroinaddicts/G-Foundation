#pragma once
#include "SceneDocument.h"
#include <string_view>

// Shared authoring-data adaptation. Runtime owns binding and playback.
namespace SceneAnimationDocument {
bool Handles(std::string_view type);
void Validate(const Json::Value& data, const std::string& path);
void Create(const Json::Value& data, GameSproutRuntime::IGameObject& object);
Json::Value Serialize(GameSproutRuntime::IComponent& component,
    const SceneDocument::ObjectIDs& ids);
void ValidateReferences(const Json::Value& document);
void RestoreBindings(const Json::Value& snapshot, GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene& scene);
void BindAnimator(const Json::Value& component,GameSproutRuntime::IGameObject& object,
    const std::map<std::uint64_t,GameSproutRuntime::IGameObject*>& objects);
void BindSkinnedRenderer(const Json::Value& component,GameSproutRuntime::IGameObject& object,
    const std::map<std::uint64_t,GameSproutRuntime::IGameObject*>& objects);
void Bind(const Json::Value& document, GameSproutRuntime::IScene& scene,
    const std::map<std::uint64_t, GameSproutRuntime::IGameObject*>& objects);
}
