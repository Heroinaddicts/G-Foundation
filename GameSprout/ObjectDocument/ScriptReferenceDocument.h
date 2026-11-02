#pragma once
#include "JsonCpp/json.hpp"
#include <stdexcept>

// Visit typed script slots, rather than searching arbitrary JSON properties by
// name. This includes references nested in records and container keys/values.
namespace ScriptReferenceDocument {
template<typename Value,typename Visitor>
void Visit(Value& value,const Visitor& visitor,unsigned depth=0) {
    // JSON adds wrapper/key/value objects around each logical ScriptValue.
    if(depth>256)throw std::runtime_error("Script reference document nesting exceeds the value limit");
    if(value.isObject()) {
        const auto kind=value.get("Kind",Json::Value());
        if(kind=="SceneObject"||kind=="Component") {
            if(!value["Value"].isNull())visitor(value,kind=="Component");
            return;
        }
        for(const auto& name:value.getMemberNames())Visit(value[name],visitor,depth+1);
    }else if(value.isArray())for(auto& child:value)Visit(child,visitor,depth+1);
}
template<typename Value,typename Visitor>
void Fields(Value& component,const Visitor& visitor) {
    if(component["Type"]=="GameSproutRuntime.ScriptComponent")
        Visit(component["Fields"],visitor);
}
}
