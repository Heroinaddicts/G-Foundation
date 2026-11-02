#pragma once
#include "SceneScriptDocument.h"

namespace PrefabScriptFields {
// Comparison snapshots use the current declaration on both sides. This never
// writes a source or changes a Runtime component, and keeps missing-type data.
bool Align(Json::Value& record,Json::Value& current,Json::Value* source,
    const std::vector<GameSproutRuntime::ScriptTypeInfo>& schemas,std::string& error);
}
