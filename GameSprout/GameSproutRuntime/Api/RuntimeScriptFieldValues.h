#pragma once
#include "RuntimeScriptApi.h"

namespace GameSproutRuntime {
// Pure value projection shared by document restore and native type replacement.
// It never invokes code from the native module or retains an instance pointer.
inline std::map<std::string,ScriptValue> ProjectSerializedScriptFields(
    const std::map<std::string,ScriptValue>& values,const ScriptTypeInfo* type) {
    if(!type)return values;
    std::map<std::string,ScriptValue> result;
    for(const auto& field:type->Fields) {
        const auto found=values.find(field.ID);
        result.emplace(field.ID,found!=values.end()&&found->second.Kind==field.Default.Kind
            ?found->second:field.Default);
    }
    return result;
}
}
