#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/ScriptComponent.h"
#include <utility>

namespace GameSproutRuntime {
namespace {
// Resolve the registered identities after every user callback; never keep a
// native component pointer across a mutation/lifetime boundary.
ScriptComponent* Resolve(Runtime& runtime,const std::pair<std::uint64_t,std::uint64_t>& key) {
    auto* node=runtime.Find(key.first);
    if(!node) return nullptr;
    for(const auto& entry:node->Components)
        if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);script && script->GetScriptInstanceID()==key.second) return script;
    return nullptr;
}
}

bool Runtime::BindSceneObjectIdentity(IGameObject* object,std::uint64_t fileID,std::string& error) {
    auto* node=Find(object);
    if(!IsInScene(node)) {error="Object has no live scene";return false;}
    return BindSceneObjectIdentities(node->SceneOwner,{{node->ID,fileID}},error);
}

bool Runtime::BindSceneObjectIdentities(IScene* scene,const std::vector<SceneObjectIdentity>& identities,std::string& error) {
    return BindAuthoringIdentities(scene,identities,{},error);
}

bool Runtime::BindAuthoringIdentities(IScene* scene,const std::vector<SceneObjectIdentity>& identities,
    const std::vector<SceneComponentBinding>& components,std::string& error) {
    error.clear();
    auto* root=Find(scene);
    if(!IsInScene(root) || !root->IsSceneRoot() || _ShuttingDown || _Depth || _Updating || _Flushing) {
        error="Bind scene identities outside Runtime callbacks, on a live scene object";return false;
    }
    decltype(_ObjectFileIDs) byObject;
    decltype(_SceneObjectIDs) byFile;
    decltype(_ComponentFileIDs) byComponent;
    decltype(_SceneComponentIDs) componentFiles;
    for(const auto& identity:identities) {
        auto* node=Find(identity.ObjectID);
        if(!IsInScene(node) || node->SceneOwner!=scene) {error="Object belongs to another scene";return false;}
        if(node->IsSceneRoot()!=(identity.FileID==0)) {error="FileID 0 is reserved for the scene root";return false;}
        const auto key=std::make_pair(root->ID,identity.FileID);
        if(_SceneComponentIDs.contains(key)){error="Object FileID collides with a component";return false;}
        const auto collision=_SceneObjectIDs.find(key);
        if((collision!=_SceneObjectIDs.end() && collision->second!=node->ID)||
           (byFile.contains(key)&&byFile.at(key)!=node->ID)) {error="Scene FileID is already bound";return false;}
        const auto previous=_ObjectFileIDs.find(node->ID);
        if(previous!=_ObjectFileIDs.end()) {
            if(previous->second.SceneID==root->ID && previous->second.FileID==identity.FileID) continue;
            error="A live object's stable scene identity cannot be reassigned";return false;
        }
        const auto [pending,inserted]=byObject.emplace(node->ID,ObjectIdentity{root->ID,identity.FileID});
        if(!inserted&&pending->second.FileID!=identity.FileID){error="Conflicting object identities";return false;}
        byFile.emplace(key,node->ID);
    }
    for(const auto& identity:components) {
        auto* node=Find(identity.ObjectID);
        if(!IsInScene(node)||node->SceneOwner!=scene||!identity.FileID||!identity.ComponentInstanceID) {
            error="Component identity requires a live owner and nonzero IDs";return false;
        }
        const IComponent* component=nullptr;
        for(const auto& entry:node->Components)if(entry.Serial==identity.ComponentInstanceID&&!entry.PendingRemove){component=entry.Pointer;break;}
        if(!component){error="Component identity requires a live component";return false;}
        const auto key=std::make_pair(root->ID,identity.FileID);
        if(_SceneObjectIDs.contains(key)||byFile.contains(key)) {error="Component FileID collides with a scene object";return false;}
        const auto previous=_ComponentFileIDs.find(component);
        if(previous!=_ComponentFileIDs.end()) {
            const auto& value=previous->second;
            if(value.SceneID==root->ID&&value.ObjectID==node->ID&&value.InstanceID==identity.ComponentInstanceID&&value.FileID==identity.FileID)continue;
            error="A live component's stable identity cannot be reassigned";return false;
        }
        if(_SceneComponentIDs.contains(key)||componentFiles.contains(key)||byComponent.contains(component)) {
            error="Duplicate component identity";return false;
        }
        componentFiles.emplace(key,std::make_pair(node->ID,identity.ComponentInstanceID));
        byComponent.emplace(component,ComponentIdentity{root->ID,identity.FileID,node->ID,identity.ComponentInstanceID});
    }
    const bool changed=!byObject.empty()||!byComponent.empty();
    _ObjectFileIDs.merge(byObject);_SceneObjectIDs.merge(byFile);
    _ComponentFileIDs.merge(byComponent);_SceneComponentIDs.merge(componentFiles);
    if(changed)RefreshScriptObjectReferences();
    return true;
}

IGameObject* Runtime::QuerySceneObjectByFileID(const IScene* scene,std::uint64_t fileID) const {
    auto* sceneNode=Find(const_cast<IScene*>(scene));
    if(!IsInScene(sceneNode) || !sceneNode->IsSceneRoot()) return nullptr;
    const auto identity=_SceneObjectIDs.find({sceneNode->ID,fileID});
    if(identity==_SceneObjectIDs.end()) return nullptr;
    auto* node=Find(identity->second);
    return IsInScene(node) && node->SceneOwner==sceneNode->SceneOwner?node->Object:nullptr;
}

bool Runtime::GetSceneObjectFileID(const IGameObject* object,std::uint64_t& fileID) const {
    auto* node=Find(const_cast<IGameObject*>(object));
    // Bound identity remains readable during destruction so fixups retain Missing IDs.
    if(!node||!node->SceneOwner||IsParked(node)) return false;
    const auto found=_ObjectFileIDs.find(node->ID);
    if(found==_ObjectFileIDs.end() || found->second.SceneID!=node->SceneOwner->GetInstanceID()) return false;
    fileID=found->second.FileID;return true;
}

void Runtime::ForgetSceneObjectIdentity(std::uint64_t objectID) {
    const auto found=_ObjectFileIDs.find(objectID);
    if(found==_ObjectFileIDs.end()) return;
    _SceneObjectIDs.erase({found->second.SceneID,found->second.FileID});
    _ObjectFileIDs.erase(found);
}

void Runtime::InvalidateScriptObjectReferences(IGameObject* object) {
    if(!_ScriptExecutionEnabled) return;
    RuntimeOperation operation(*this);
    const auto identity=_ObjectFileIDs.find(object->GetInstanceID());
    for(const auto& key:SnapshotScriptIdentities()) if(auto* script=Resolve(*this,key)) {
        bool affected=script->InvalidateObjectReference(object);
        auto* node=Find(key.first);
        if(identity!=_ObjectFileIDs.end() && node && node->SceneOwner && node->SceneOwner->GetInstanceID()==identity->second.SceneID)
            for(const auto& field:script->GetFields())
                if(field.second.Kind==ScriptValueKind::SceneObject && field.second.SceneObject.Assigned &&
                   field.second.SceneObject.ObjectFileID==identity->second.FileID) affected=true;
        if(affected && IsInScene(node)) Notify(*node,SceneNodeEventType::ComponentChanged,script->GetTypeName());
    }
}

void Runtime::RefreshScriptObjectReferences() {
    // During source-scene assembly there are no native user instances. Binding
    // N identities must not scan N scene graphs before execution even starts.
    if(!_ScriptExecutionEnabled) return;
    RuntimeOperation operation(*this);
    for(const auto& key:SnapshotScriptIdentities()) if(auto* script=Resolve(*this,key))
        if(script->RebindMissingObjectReferences()) if(auto* node=Find(key.first);IsInScene(node))
            Notify(*node,SceneNodeEventType::ComponentChanged,script->GetTypeName());
}
}
