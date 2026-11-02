#include "ScriptComponent.h"
#include "../Api/RuntimeScriptFieldValues.h"
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
bool ScriptComponent::RestoreSerializedFields(const std::map<std::string,ScriptValue>& values,std::string& error) {
    error.clear();
    if(_Instance||_InvocationDepth||_Runtime.IsExecuting()) {
        error="Restore serialized script fields outside Runtime callbacks and before native execution starts";return false;
    }
    // Validate the full stored snapshot before projecting it. A removed field
    // must not turn malformed values into acceptable persistent data.
    if(values.size()>4096){error="Too many serialized script fields";return false;}
    for(const auto& [id,value]:values)if(id.empty()||id.size()>1024||id.find('\0')!=std::string::npos||!ValidateScriptValue(value,error,true)) {
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
