#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include <algorithm>

namespace GameSproutRuntime {
const std::string& Runtime::TagName(std::uint64_t id) const {
    const auto found=_TagNames.find(id);
    static const std::string empty;
    return found==_TagNames.end()?empty:found->second;
}
bool Runtime::CanApplyProjectSettings(const RuntimeProjectSettings& input,std::string& error) const {
    error.clear();
    if(IsExecuting() || IsConstructing() || _ShuttingDown) {error="Cannot replace project settings inside a Runtime operation";return false;}
    if(!ValidateProjectSettings(input,error))return false;
    if(input.NextTagID<_ProjectSettings.NextTagID){error="Tag identity high-water mark cannot move backwards";return false;}
    std::set<std::uint64_t> defined;
    for(const auto& tag:input.Tags) {
        if(tag.ID<_ProjectSettings.NextTagID && !_TagNames.count(tag.ID)) {
            error="Deleted Tag identities cannot be reused";return false;
        }
        defined.insert(tag.ID);
    }
    for(const auto& entry:_TaggedObjects)
        if(!entry.second.empty() && !defined.count(entry.first)) {error="Cannot remove a Tag referenced by a live object";return false;}
    return true;
}
bool Runtime::SetProjectSettings(const RuntimeProjectSettings& input,std::string& error) {
    if(!CanApplyProjectSettings(input,error))return false;
    std::map<std::uint64_t,std::string> names;
    std::map<std::string,std::uint64_t> identities;
    for(const auto& tag:input.Tags) {names.emplace(tag.ID,tag.Name);identities.emplace(tag.Name,tag.ID);}
    if(input==_ProjectSettings)return true;
    // Build every allocating value before publication. Matrices are consumed on
    // the next physics synchronization; ray query masks remain independent.
    RuntimeProjectSettings candidate=input;
    std::vector<std::uint64_t> renamed;
    for(const auto& entry:_TagNames) {
        const auto next=names.find(entry.first);
        if(next!=names.end() && next->second!=entry.second)renamed.push_back(entry.first);
    }
    std::swap(_ProjectSettings,candidate);_TagNames.swap(names);_TagIDs.swap(identities);
    for(const auto id:renamed) {
        const auto found=_TaggedObjects.find(id);
        if(found!=_TaggedObjects.end())for(const auto objectID:found->second)
            if(auto* object=Find(objectID))Notify(*object,SceneNodeEventType::TagChanged);
    }
    return true;
}
bool Runtime::SetObjectTag(GameObjectState& node,std::uint64_t id) {
    if(!IsUsable(&node) || !_TagNames.count(id))return false;
    if(node.TagID==id)return true;
    _TaggedObjects[id].insert(node.ID);
    ForgetObjectTag(node);node.TagID=id;
    Notify(node,SceneNodeEventType::TagChanged);return true;
}
void Runtime::ForgetObjectTag(const GameObjectState& node) {
    const auto found=_TaggedObjects.find(node.TagID);
    if(found==_TaggedObjects.end())return;
    found->second.erase(node.ID);
    if(found->second.empty())_TaggedObjects.erase(found);
}
std::vector<IGameObject*> Runtime::FindGameObjectsWithTag(const std::string& name,const IScene* scene) const {
    std::vector<IGameObject*> result;
    const auto tag=_TagIDs.find(name);
    if(tag==_TagIDs.end())return result;
    if(scene) {
        const auto found=_ByPointer.find(const_cast<IScene*>(scene));
        if(found==_ByPointer.end() || !IsInScene(found->second) || !found->second->IsSceneRoot())return result;
    }
    const auto found=_TaggedObjects.find(tag->second);
    if(found!=_TaggedObjects.end())for(const auto id:found->second) {
        auto* node=Find(id);
        if(node && !node->IsSceneRoot() && node->IsActiveInHierarchy() && (!scene || node->SceneOwner==scene))
            result.push_back(node->Object);
    }
    return result;
}
IGameObject* Runtime::FindGameObjectWithTag(const std::string& name,const IScene* scene) const {
    const auto tag=_TagIDs.find(name);
    if(tag==_TagIDs.end())return nullptr;
    if(scene) {
        const auto found=_ByPointer.find(const_cast<IScene*>(scene));
        if(found==_ByPointer.end() || !IsInScene(found->second) || !found->second->IsSceneRoot())return nullptr;
    }
    const auto found=_TaggedObjects.find(tag->second);
    if(found!=_TaggedObjects.end())for(const auto id:found->second) {
        auto* node=Find(id);
        if(node && !node->IsSceneRoot() && node->IsActiveInHierarchy() && (!scene || node->SceneOwner==scene))return node->Object;
    }
    return nullptr;
}
}
