#pragma once
#include "../Shared/PrefabInstanceDocument.h"
#include "ISceneManager.h"

namespace PrefabRecords {
inline Json::Value Mappings(const SceneSubtreeDocument& result) {
    Json::Value values(Json::arrayValue);
    for(const auto& mapping:result.ObjectIDs) {
        Json::Value value;value["SourceID"]=Json::UInt64(mapping.DocumentFileID);value["SceneID"]=Json::UInt64(mapping.SceneFileID);
        value["Component"]=mapping.Component;values.append(value);
    }
    return values;
}
}
