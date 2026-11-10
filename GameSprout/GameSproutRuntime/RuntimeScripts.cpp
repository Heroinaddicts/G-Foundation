#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/ScriptComponent.h"
#include "Api/RuntimeScriptFieldValues.h"
#include "Components/ComponentFactory.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>

namespace GameSproutRuntime {
namespace {
struct BuiltinEntry {const char* ID;const char* Name;const char* Category;const std::type_info* Type;bool Intrinsic;};
const std::vector<BuiltinEntry>& Builtins() {
    static const std::vector<BuiltinEntry> entries={
        {"GameSproutRuntime.Transform","Transform","Transform",&typeid(ITransformComponent),true},
        {"GameSproutRuntime.RectTransform","Rect Transform","UI",&typeid(IRectTransformComponent),false},
        {"GameSproutRuntime.Canvas","Canvas","UI",&typeid(ICanvasComponent),false},
        {"GameSproutRuntime.Image","Image","UI",&typeid(IImageComponent),false},
        {"GameSproutRuntime.Text","Text","UI",&typeid(ITextComponent),false},
        {"GameSproutRuntime.Button","Button","UI",&typeid(IButtonComponent),false},
        {"GameSproutRuntime.Mesh","Mesh","Rendering",&typeid(IMeshComponent),false},
        {"GameSproutRuntime.MeshRenderer","Mesh Renderer","Rendering",&typeid(IMeshRendererComponent),false},
        {"GameSproutRuntime.SkinnedMeshRenderer","Skinned Mesh Renderer","Rendering",&typeid(ISkinnedMeshRendererComponent),false},
        {"GameSproutRuntime.Camera","Camera","Rendering",&typeid(ICameraComponent),false},
        {"GameSproutRuntime.Light","Light","Rendering",&typeid(ILightComponent),false},
        {"GameSproutRuntime.Animator","Animator","Animation",&typeid(IAnimatorComponent),false},
        {"GameSproutRuntime.Rigidbody","Rigidbody","Physics",&typeid(IRigidbodyComponent),false},
        {"GameSproutRuntime.BoxCollider","Box Collider","Physics",&typeid(IBoxColliderComponent),false},
        {"GameSproutRuntime.SphereCollider","Sphere Collider","Physics",&typeid(ISphereColliderComponent),false},
        {"GameSproutRuntime.CapsuleCollider","Capsule Collider","Physics",&typeid(ICapsuleColliderComponent),false},
        {"GameSproutRuntime.MeshCollider","Mesh Collider","Physics",&typeid(IMeshColliderComponent),false}
    };
    return entries;
}
}
std::vector<BuiltinComponentInfo> Runtime::GetBuiltinComponentTypes() const {
    std::vector<BuiltinComponentInfo> result;
    for(const auto& entry:Builtins()) result.push_back({entry.ID,entry.Name,entry.Category,entry.Intrinsic,AllowsMultipleBuiltinComponents(*entry.Type)});
    return result;
}
IComponent* Runtime::AddBuiltinComponent(IGameObject* object,const std::string& type,std::string& error) {
    error.clear();
    auto* node=Find(object);
    if(!IsUsable(node)||object==object->GetScene()) {error="Select a game object before adding a component";return nullptr;}
    for(const auto& entry:Builtins()) if(type==entry.ID) {
        if(entry.Intrinsic||(!AllowsMultipleBuiltinComponents(*entry.Type) && node->GetComponent(*entry.Type))) {error="Component already exists";return nullptr;}
        auto* result=node->AddComponent(*entry.Type);
        if(!result) error="Cannot create component: "+type;
        return result;
    }
    error="Unknown built-in component: "+type;return nullptr;
}
bool Runtime::RemoveBuiltinComponent(IGameObject* object,const std::string& type,std::string& error) {
    error.clear();auto* node=Find(object);
    if(!IsUsable(node)) {error="Object is unavailable";return false;}
    for(const auto& entry:Builtins()) if(type==entry.ID) {
        if(entry.Intrinsic) {error="The intrinsic Transform cannot be removed";return false;}
        if(node->RemoveComponent(*entry.Type)) return true;
        error="Component is unavailable";return false;
    }
    error="Unknown built-in component: "+type;return false;
}
const ScriptTypeRegistration* Runtime::FindScriptType(const std::string& id) const {
    const auto found=_ScriptTypes.find(id);return found==_ScriptTypes.end()?nullptr:&found->second;
}
const ScriptTypeRegistration* Runtime::FindScriptType(const std::type_info& type) const {
    for(const auto& entry:_ScriptTypes)
        if(entry.second.CppType && *entry.second.CppType==type) return &entry.second;
    return nullptr;
}
std::vector<ScriptTypeInfo> Runtime::GetScriptTypes() const {
    std::vector<ScriptTypeInfo> types;
    for(const auto& pair:_ScriptTypes) types.push_back(pair.second.Info);
    return types;
}
bool Runtime::RegisterScriptType(const ScriptTypeRegistration& registration,std::string& error) {
    return ReplaceScriptTypes({}, {registration}, error);
}
bool Runtime::ReplaceScriptTypes(const std::vector<std::string>& removeTypes,
    const std::vector<ScriptTypeRegistration>& addTypes,std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown||_ScriptExecutionEnabled) {error="Stop script execution before changing the type catalog";return false;}
    RuntimeOperation operation(*this);
    try {
        auto candidate=_ScriptTypes;
        std::set<std::string> removed,added;
        for(const auto& id:removeTypes) {
            if(!removed.insert(id).second||!candidate.erase(id)) {error="Script type is not registered or repeated: "+id;return false;}
        }
        for(const auto& registration:addTypes) {
            if(registration.Info.TypeID.empty()||registration.Info.Name.empty()||!registration.CppType||!registration.Create||!registration.Destroy) {error="Invalid script type descriptor";return false;}
            const auto& id=registration.Info.TypeID;
            if(candidate.contains(id)||!added.insert(id).second) {error="Duplicate script type ID: "+id;return false;}
            std::set<std::string> fields;
            ScriptTypeRegistration type=registration;
            type.Info.Fields.clear();
            for(const auto& field:type.Fields) {
                if(field.Info.ID.empty()||!fields.insert(field.Info.ID).second||!field.Read||!field.Write||!field.Validate) {error="Invalid or duplicate script field";return false;}
                if(!ValidateScriptValue(field.Info.Default,error,true)||!ValidateScriptFieldValue(field.Info,field.Info.Default,error)) {error="Invalid script field default: "+error;return false;}
                type.Info.Fields.push_back(field.Info);
            }
            candidate.emplace(id,std::move(type));
        }
        std::set<std::type_index> cppTypes;
        for(const auto& entry:candidate)
            if(!cppTypes.emplace(*entry.second.CppType).second) {error="Duplicate native C++ script type: "+entry.first;return false;}
        struct AuthoredFields {ScriptComponent* Component;std::map<std::string,ScriptValue> Values;};
        std::vector<AuthoredFields> authored;
        for(const auto& node:_Nodes) for(const auto& entry:node.second->Components) {
            auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);
            if(!script||(!removed.contains(script->GetScriptTypeID())&&!added.contains(script->GetScriptTypeID()))) continue;
            const auto type=candidate.find(script->GetScriptTypeID());
            // Missing types retain all values. Present types use exactly their
            // current declared fields, retaining values only of matching kinds.
            if(type==candidate.end()) continue;
            AuthoredFields item{script,ProjectSerializedScriptFields(script->GetFields(),&type->second.Info)};
            authored.push_back(std::move(item));
        }
        // Every validation and allocation precedes publication. These swaps
        // cannot fail, so rejected native modules never alter scene author data.
        _ScriptTypes.swap(candidate);
        for(auto& item:authored) item.Component->ReplaceAuthoredFields(item.Values);
        for(const auto& node:_Nodes) for(const auto& entry:node.second->Components)
            if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);script&&(removed.contains(script->GetScriptTypeID())||added.contains(script->GetScriptTypeID())))
                Notify(*node.second,SceneNodeEventType::ComponentChanged,script->GetTypeName());
        return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
    catch(...) {error="Cannot prepare native script type catalog";return false;}
}
bool Runtime::UnregisterScriptType(const std::string& id,std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown) {error="Cannot unload script code during a Runtime callback";return false;}
    const auto found=_ScriptTypes.find(id);
    if(found==_ScriptTypes.end()) {error="Script type is not registered: "+id;return false;}
    {
        RuntimeOperation operation(*this);
        std::vector<ScriptComponent*> scripts;
        for(const auto& node:_Nodes) for(const auto& entry:node.second->Components)
            if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);script&&script->GetScriptTypeID()==id) scripts.push_back(script);
        for(auto* script:scripts) {
            script->Stop();
            if(auto* node=Find(script->GetGameObject());IsUsable(node)) Notify(*node,SceneNodeEventType::ComponentChanged,script->GetTypeName());
        }
    }
    // std::function closures are destroyed before the shell unloads the DLL.
    _ScriptTypes.erase(found);return true;
}
IScriptComponent* Runtime::AddScriptComponent(IGameObject* object,const std::string& type,const ResourceReference& source,std::uint64_t id,std::string& error) {
    error.clear();auto* node=Find(object);
    if(!IsUsable(node)||object==object->GetScene()||type.empty()||source.Guid.empty()) {error="A script requires a game object, stable type ID and source asset GUID";return nullptr;}
    if(!id) {
        if(node->NextScriptID==std::numeric_limits<std::uint64_t>::max()) {error="Script instance IDs are exhausted";return nullptr;}
        id=node->NextScriptID+1;
    }
    for(const auto& entry:node->Components)
        if(const auto* script=dynamic_cast<IScriptComponent*>(entry.Pointer)) {
            if(script->GetScriptTypeID()==type) {error="This object already contains the script type: "+type;return nullptr;}
            if(script->GetScriptInstanceID()==id) {error="Duplicate script instance ID";return nullptr;}
        }
    if(node->NextComponentSerial==std::numeric_limits<std::uint64_t>::max()) {error="Component serials are exhausted";return nullptr;}
    RuntimeOperation operation(*this);
    auto* script=new ScriptComponent(*this,object,type,source,id);
    script->_ComponentInstanceID=node->NextComponentSerial+1;
    bool tickRegistered=false;
    try {
        node->Components.push_back({script,++node->NextComponentSerial,false});
        RegisterComponentTick(node->ID,AllTicks);tickRegistered=true;
        RegisterScriptIdentity(node->ID,id);
    }catch(...) {
        if(tickRegistered)UnregisterComponentTick(node->ID,AllTicks);
        UnregisterScriptIdentity(node->ID,id);
        if(!node->Components.empty()&&node->Components.back().Pointer==script)node->Components.pop_back();
        delete script;error="Cannot allocate script component";return nullptr;
    }
    node->NextScriptID=std::max(node->NextScriptID,id);
    QueueComponentInitialization(*node,node->NextComponentSerial);
    Notify(*node,SceneNodeEventType::ComponentAdded,script->GetTypeName());
    return script;
}
bool Runtime::RemoveScriptComponent(IGameObject* object,std::uint64_t id,std::string& error) {
    error.clear();auto* node=Find(object);
    if(!IsUsable(node)) {error="Object is unavailable";return false;}
    RuntimeOperation operation(*this);
    for(auto& entry:node->Components) {
        const auto* script=dynamic_cast<IScriptComponent*>(entry.Pointer);
        if(!script||script->GetScriptInstanceID()!=id) continue;
        if(entry.PendingRemove) return true;
        if(!RequestRemoveComponent(node,entry.Pointer,entry.Serial)) break;
        entry.PendingRemove=true;return true;
    }
    error="Script component is unavailable";return false;
}
bool Runtime::SetScriptExecutionEnabled(bool enabled,std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown||_ConstructionDepth) {error="Cannot change script execution during a Runtime callback or construction";return false;}
    if(!enabled)SetInputEnabled(false);
    if(_ScriptExecutionEnabled==enabled) return true;
    RuntimeOperation operation(*this);
    std::vector<ScriptComponent*> scripts;
    for(auto current=_ScriptIdentities.begin();current!=_ScriptIdentities.end();) {
        const auto objectID=current->first;
        if(auto* node=Find(objectID);node&&!IsParked(node))for(const auto& entry:node->Components)
            if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer)) scripts.push_back(script);
        current=_ScriptIdentities.upper_bound({objectID,std::numeric_limits<std::uint64_t>::max()});
    }
    // Resolve native cross-references while every instance is still alive.
    // OnRelease ordering must never expose a peer that has already been freed.
    if(!enabled)for(auto* script:scripts)InvalidateNativeScriptReferences(script);
    _ScriptExecutionEnabled=enabled;
    for(auto* script:scripts)script->Stop();
    for(auto* script:scripts) {
        script->BindDefaults();
        QueueScriptInitialization(*script);
    }
    return true;
}
void Runtime::NotifyScriptError(IGameObject* object,const std::string& error) {
    auto* node=Find(object);if(!IsInScene(node)||_ShuttingDown) return;
    SceneNodeEvent event;
    event.Type=SceneNodeEventType::OperationFailed;event.SceneID=object->GetScene()->GetInstanceID();
    event.ObjectID=node->ID;event.ComponentType="GameSproutRuntime.ScriptComponent";event.Error=error;
    _Events.push_back({++_EventSequence,std::move(event)});
}
}
