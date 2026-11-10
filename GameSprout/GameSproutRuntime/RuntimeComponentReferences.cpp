#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/ScriptComponent.h"

namespace GameSproutRuntime {
IComponent* Runtime::NormalizeComponent(const IComponent* input) const {
    if(!input)return nullptr;
    // The input may be a stale address, so never call through it before finding
    // a live Runtime entry. Native script instances map to their authored wrapper.
    for(const auto& [id,node]:_Nodes) {
        if(IsParked(node))continue;
        for(const auto& entry:node->Components) {
            if(entry.Pointer==input)return entry.Pointer;
            if(const auto* script=dynamic_cast<const ScriptComponent*>(entry.Pointer);
               script&&script->PeekNativeInstance()==input)return entry.Pointer;
        }
    }
    return nullptr;
}
bool Runtime::IsComponentType(const IComponent* input,const std::string& type) const {
    const auto* component=NormalizeComponent(input);if(!component)return false;
    if(type=="GameSproutRuntime.Component")return true;
    if(type=="GameSproutRuntime.Collider")return dynamic_cast<const IColliderComponent*>(component)!=nullptr;
    if(const auto* script=dynamic_cast<const IScriptComponent*>(component))
        return type==script->GetScriptTypeID()||type=="GameSproutRuntime.Script"||type=="GameSproutRuntime.ScriptComponent";
    return type==component->GetTypeName();
}
bool Runtime::SetSceneComponentIdentities(IScene* scene,const std::vector<SceneComponentIdentity>& identities,std::string& error) {
    error.clear();auto* root=Find(scene);
    if(!IsInScene(root)||!root->IsSceneRoot()||_ShuttingDown||_Depth||_Updating||_Flushing) {
        error="Set component identities outside Runtime callbacks on a live scene";return false;
    }
    auto byComponent=_ComponentFileIDs;auto byFile=_SceneComponentIDs;
    for(auto it=byComponent.begin();it!=byComponent.end();) {
        if(it->second.SceneID==root->ID)it=byComponent.erase(it);else ++it;
    }
    for(auto it=byFile.begin();it!=byFile.end();) {
        if(it->first.first==root->ID)it=byFile.erase(it);else ++it;
    }
    std::map<const IComponent*,std::pair<IComponent*,GameObjectState*>> live;
    for(const auto& [id,node]:_Nodes)if(IsInScene(node)&&node->SceneOwner==scene)
        for(const auto& entry:node->Components)if(!entry.PendingRemove) {
            live.emplace(entry.Pointer,std::make_pair(entry.Pointer,node));
            if(const auto* script=dynamic_cast<const ScriptComponent*>(entry.Pointer))
                if(const auto* native=script->PeekNativeInstance())live.emplace(native,std::make_pair(entry.Pointer,node));
        }
    for(const auto& identity:identities) {
        const auto found=live.find(identity.Component);
        if(found==live.end()||!identity.FileID){error="Component requires a live instance and nonzero FileID";return false;}
        auto* component=found->second.first;auto* node=found->second.second;
        if(_SceneObjectIDs.contains({root->ID,identity.FileID})) {error="Component FileID collides with a scene object";return false;}
        const auto key=std::make_pair(root->ID,identity.FileID);
        if(byFile.contains(key)||byComponent.contains(component)) {error="Duplicate component identity";return false;}
        const auto instance=component->GetComponentInstanceID();
        if(!instance){error="Component has no live instance identity";return false;}
        // A surviving component keeps its identity. Removed/restored instances
        // can reuse the document FileID after the old lifetime has been removed.
        const auto existing=_ComponentFileIDs.find(component);
        if(existing!=_ComponentFileIDs.end()&&existing->second.ObjectID==node->ID&&
           existing->second.InstanceID==instance&&existing->second.FileID!=identity.FileID) {
            error="A live component's stable identity cannot be reassigned";return false;
        }
        byFile.emplace(key,std::make_pair(node->ID,instance));
        byComponent.emplace(component,ComponentIdentity{root->ID,identity.FileID,node->ID,instance});
    }
    _ComponentFileIDs.swap(byComponent);_SceneComponentIDs.swap(byFile);
    RefreshScriptObjectReferences();return true;
}
IComponent* Runtime::QuerySceneComponentByFileID(const IScene* scene,std::uint64_t fileID) const {
    auto* root=Find(const_cast<IScene*>(scene));if(!IsInScene(root)||!root->IsSceneRoot()||!fileID)return nullptr;
    const auto found=_SceneComponentIDs.find({root->ID,fileID});if(found==_SceneComponentIDs.end())return nullptr;
    auto* node=Find(found->second.first);if(!IsInScene(node)||node->SceneOwner!=scene)return nullptr;
    for(const auto& entry:node->Components)if(entry.Serial==found->second.second&&!entry.PendingRemove)return entry.Pointer;
    return nullptr;
}
bool Runtime::GetSceneComponentFileID(const IComponent* input,std::uint64_t& fileID) const {
    // Most reads use an authored wrapper; resolve that indexed identity without
    // rescanning the hierarchy. Native script pointers need normalization once.
    auto found=_ComponentFileIDs.find(input);
    if(found==_ComponentFileIDs.end()) {
        auto* canonical=NormalizeComponent(input);if(!canonical)return false;
        found=_ComponentFileIDs.find(canonical);if(found==_ComponentFileIDs.end())return false;
    }
    auto* node=Find(found->second.ObjectID);
    if(!node||!node->SceneOwner||IsParked(node)||node->SceneOwner->GetInstanceID()!=found->second.SceneID)return false;
    bool live=false;
    for(const auto& entry:node->Components)if(entry.Pointer==found->first&&entry.Serial==found->second.InstanceID){live=true;break;}
    if(!live)return false;
    fileID=found->second.FileID;return true;
}
void Runtime::ForgetSceneComponentIdentity(const IComponent* component) {
    const auto found=_ComponentFileIDs.find(component);if(found==_ComponentFileIDs.end())return;
    _SceneComponentIDs.erase({found->second.SceneID,found->second.FileID});_ComponentFileIDs.erase(found);
}
void Runtime::InvalidateNativeScriptReferences(ScriptComponent* target) {
    if(!_ScriptExecutionEnabled||!target||!target->PeekNativeInstance())return;
    RuntimeOperation operation(*this);
    const auto scripts=SnapshotScriptIdentities();
    for(const auto& [id,instance]:scripts) {
        auto* node=Find(id);if(!node||node->Destroying)continue;
        for(const auto& entry:node->Components)if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);
            script&&script!=target&&script->GetScriptInstanceID()==instance) {
            if(script->InvalidateNativeInstanceReference(target)&&IsInScene(node))Notify(*node,SceneNodeEventType::ComponentChanged,script->GetTypeName());
            break;
        }
    }
}

void Runtime::InvalidateScriptComponentReferences(IComponent* component) {
    if(_ScriptExecutionEnabled) {
        RuntimeOperation operation(*this);
        const auto scripts=SnapshotScriptIdentities();
        for(const auto& [id,instance]:scripts) {
            auto* node=Find(id);if(!node||node->Destroying)continue;
            for(const auto& entry:node->Components)if(auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);
                script&&script->GetScriptInstanceID()==instance) {
                if(script->InvalidateComponentReference(component)&&IsInScene(node))Notify(*node,SceneNodeEventType::ComponentChanged,script->GetTypeName());
                break;
            }
        }
    }
    ForgetSceneComponentIdentity(component);
}
}
