#include "ScriptComponent.h"
#include <exception>

namespace GameSproutRuntime {
bool ScriptComponent::ApplyNativeField(const ScriptFieldRegistration& field,const ScriptValue& value,std::string& error) {
    if(value.Kind!=ScriptValueKind::SceneObject) return field.Write && field.Write(*_Instance,value,error);
    if(!field.WriteObject || !field.ReadObject) {error="Object field has no native binding";return false;}
    auto* target=value.SceneObject.Assigned?
        _Runtime.QuerySceneObjectByFileID(_GameObject->GetScene(),value.SceneObject.ObjectFileID):nullptr;
    if(!field.WriteObject(*_Instance,target,error)) return false;
    if(value.SceneObject.Assigned && !target) _MissingObjectFields.insert(field.Info.ID);
    else _MissingObjectFields.erase(field.Info.ID);
    return true;
}

bool ScriptComponent::InvalidateObjectReference(IGameObject* object) {
    if(!_Instance || !object) return false;
    const auto* type=_Runtime.FindScriptType(_TypeID);
    if(!type) return false;
    bool changed=false;
    try {
        for(const auto& field:type->Fields) {
            if(field.Info.Default.Kind!=ScriptValueKind::SceneObject || !field.ReadObject || !field.WriteObject) continue;
            // Read the actual member, not only the authored ID. User code may
            // have assigned another object since the last Inspector edit.
            if(field.ReadObject(*_Instance)!=object) continue;
            std::string error;
            if(!field.WriteObject(*_Instance,nullptr,error)) {Fail("Cannot invalidate object field: "+error);return true;}
            changed=true;
            const auto authored=_Fields.find(field.Info.ID);
            if(authored!=_Fields.end() && authored->second.SceneObject.Assigned) _MissingObjectFields.insert(field.Info.ID);
        }
    } catch(const std::exception& exception) {Fail(exception.what());}
      catch(...) {Fail("Cannot invalidate a native object reference");}
    return changed;
}

bool ScriptComponent::RebindMissingObjectReferences() {
    if(!_Instance || _MissingObjectFields.empty()) return false;
    const auto* type=_Runtime.FindScriptType(_TypeID);
    if(!type) return false;
    bool changed=false;
    try {
        for(const auto& field:type->Fields) {
            if(!_MissingObjectFields.contains(field.Info.ID)) continue;
            const auto authored=_Fields.find(field.Info.ID);
            if(authored==_Fields.end() || !authored->second.SceneObject.Assigned) continue;
            auto* target=_Runtime.QuerySceneObjectByFileID(_GameObject->GetScene(),authored->second.SceneObject.ObjectFileID);
            if(!target) continue;
            // An explicit gameplay reassignment to a live pointer wins over a
            // later undo/restore of the originally authored target.
            if(field.ReadObject && field.ReadObject(*_Instance)) {_MissingObjectFields.erase(field.Info.ID);continue;}
            std::string error;
            if(!ApplyNativeField(field,authored->second,error)) {Fail("Cannot restore object field: "+error);return true;}
            changed=true;
        }
    } catch(const std::exception& exception) {Fail(exception.what());}
      catch(...) {Fail("Cannot restore a native object reference");}
    return changed;
}
}
