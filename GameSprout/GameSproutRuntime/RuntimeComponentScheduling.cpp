#include "GameSproutRuntime.h"
#include "GameObject.h"
#include <limits>

namespace GameSproutRuntime {
void Runtime::RegisterAnimationComponent(std::uint64_t objectID,const IComponent* component) {
    if(component->GetComponentType()==typeid(IAnimatorComponent)||component->GetComponentType()==typeid(ISkinnedMeshRendererComponent))
        ++_AnimationObjects[objectID];
}
void Runtime::UnregisterAnimationComponent(std::uint64_t objectID,const IComponent* component) {
    if(component->GetComponentType()!=typeid(IAnimatorComponent)&&component->GetComponentType()!=typeid(ISkinnedMeshRendererComponent))return;
    const auto found=_AnimationObjects.find(objectID);if(found==_AnimationObjects.end())return;
    if(found->second>1)--found->second;else _AnimationObjects.erase(found);
}

void Runtime::RegisterComponentTick(std::uint64_t objectID,std::uint8_t mask) {
    if(!mask)return;
    auto& counts=_TickObjects[objectID];
    if(mask&1)++counts.Variable;
    if(mask&2)++counts.Fixed;
}
void Runtime::UnregisterComponentTick(std::uint64_t objectID,std::uint8_t mask) {
    if(!mask)return;
    const auto found=_TickObjects.find(objectID);if(found==_TickObjects.end())return;
    auto& counts=found->second;
    if((mask&1)&&counts.Variable)--counts.Variable;
    if((mask&2)&&counts.Fixed)--counts.Fixed;
    if(!counts.Variable&&!counts.Fixed)_TickObjects.erase(found);
}
void Runtime::RegisterScriptIdentity(std::uint64_t objectID,std::uint64_t instanceID) {
    _ScriptIdentities.emplace(objectID,instanceID);
}
void Runtime::UnregisterScriptIdentity(std::uint64_t objectID,std::uint64_t instanceID) {
    _ScriptIdentities.erase({objectID,instanceID});
}
std::vector<std::pair<std::uint64_t,std::uint64_t>> Runtime::SnapshotScriptIdentities() const {
    std::vector<std::pair<std::uint64_t,std::uint64_t>> result;
    result.reserve(_ScriptIdentities.size());
    for(auto current=_ScriptIdentities.begin();current!=_ScriptIdentities.end();) {
        const auto objectID=current->first;
        // Keep the existing node/component order, including explicit component
        // reordering, without searching unrelated static scene objects.
        if(auto* node=Find(objectID))for(const auto& entry:node->Components)
            if(auto* script=dynamic_cast<IScriptComponent*>(entry.Pointer))
                result.emplace_back(objectID,script->GetScriptInstanceID());
        current=_ScriptIdentities.upper_bound({objectID,std::numeric_limits<std::uint64_t>::max()});
    }
    return result;
}
}
