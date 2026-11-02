#pragma once
#include "SceneDocument.h"

// Native 脚本的作者数据；不在编辑器打开场景时执行用户代码。
namespace SceneScriptDocument {
void Validate(const Json::Value& data, const std::string& path);
void Create(const Json::Value& data, GameSproutRuntime::IGameObject& object,
            GameSproutRuntime::IGameSproutRuntime& runtime);
Json::Value Serialize(GameSproutRuntime::IComponent& component);
}
