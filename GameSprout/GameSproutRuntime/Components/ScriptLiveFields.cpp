#include "ScriptComponent.h"
#include "../GameObject.h"
#include "../Api/RuntimeScriptFieldValues.h"
#include "../Scene.h"
#include <exception>

namespace GameSproutRuntime {
bool ScriptComponent::ReadFields(std::map<std::string,ScriptValue>& snapshot,std::string& error) const {
    error.clear();
    try {
        RuntimeOperation operation(_Runtime);
        InvocationScope reading(*const_cast<ScriptComponent*>(this),false);
        auto values=_Fields;
        const auto* type=_Runtime.FindScriptType(_TypeID);
        if(_Instance&&type&&!_StopRequested) {
            for(const auto& field:type->Fields) {
                ScriptValue value=field.Info.Default;
                if(!field.Read||!field.Read(*_Instance,value,ReferenceContext(),error)) {
                    if(error.empty())error="Cannot read native script field: "+field.Info.ID;
                    return false;
                }
                if(value.Kind!=field.Info.Default.Kind){error="Native script reader returned a mismatched value kind: "+field.Info.ID;return false;}
                if(!ValidateScriptValue(value,error)||!ValidateScriptFieldValue(field.Info,value,error))return false;
                values[field.Info.ID]=std::move(value);
            }
        }
        snapshot.swap(values);return true;
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="Cannot read native script fields";}
    return false;
}
}
