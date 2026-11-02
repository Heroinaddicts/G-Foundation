#pragma once
#include "SceneDocument.h"

// Native 脚本的作者数据；不在编辑器打开场景时执行用户代码。
namespace SceneScriptDocument {
void Validate(const Json::Value& data, const std::string& path);
void Create(const Json::Value& data, GameSproutRuntime::IGameObject& object,
            GameSproutRuntime::IGameSproutRuntime& runtime);
Json::Value Serialize(GameSproutRuntime::IComponent& component);
// Pure document projection for comparison/merge against the current script
// declarations. Missing types retain their stored values; IDs are not remapped.
// Invalid stored data rejects atomically. This does not execute native scripts.
bool AlignFields(Json::Value& document,
    const std::vector<GameSproutRuntime::ScriptTypeInfo>& schemas,std::string& error);
}
