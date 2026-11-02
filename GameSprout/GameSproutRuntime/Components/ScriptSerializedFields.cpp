#include "ScriptComponent.h"
#include "../Api/RuntimeScriptFieldValues.h"
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
namespace {
bool ValidSerializedValue(const ScriptValue& value) {
    switch(value.Kind) {
    case ScriptValueKind::Boolean:case ScriptValueKind::Integer:return true;
    case ScriptValueKind::Number:return std::isfinite(value.Number);
    case ScriptValueKind::String:return value.String.size()<=1024*1024&&value.String.find('\0')==std::string::npos;
    case ScriptValueKind::Vector3:return std::isfinite(value.Vector.X)&&std::isfinite(value.Vector.Y)&&std::isfinite(value.Vector.Z);
    case ScriptValueKind::SceneObject:return value.SceneObject.Assigned||value.SceneObject.ObjectFileID==0;
    case ScriptValueKind::Resource: {
        if(value.Resource.Guid.empty())return value.Resource.LocalID==0;
        if(!value.Resource.LocalID||value.Resource.Guid.size()!=32)return false;
        bool nonzero=false;
        for(const auto digit:value.Resource.Guid) {
            if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')))return false;
            nonzero=nonzero||digit!='0';
        }
        return nonzero;
    }
    }
    return false;
}
}
bool ScriptComponent::RestoreSerializedFields(const std::map<std::string,ScriptValue>& values,std::string& error) {
    error.clear();
    if(_Instance||_InvocationDepth||_Runtime.IsExecuting()) {
        error="Restore serialized script fields outside Runtime callbacks and before native execution starts";return false;
    }
    // Validate the full stored snapshot before projecting it. A removed field
    // must not turn malformed values into acceptable persistent data.
    if(values.size()>4096){error="Too many serialized script fields";return false;}
    for(const auto& [id,value]:values)if(id.empty()||id.size()>1024||id.find('\0')!=std::string::npos||!ValidSerializedValue(value)) {
        error="Invalid serialized script field: "+id;return false;
    }
    try {
        const auto* type=_Runtime.FindScriptType(_TypeID);
        auto projected=ProjectSerializedScriptFields(values,type?&type->Info:nullptr);
        // Exact replacement retains its strict command semantics and performs
        // the same atomic publication/event notification for this prepared map.
        return ReplaceFields(projected,error);
    }catch(const std::exception& exception){error=exception.what();return false;}
    catch(...){error="Cannot restore serialized script fields";return false;}
}
}
