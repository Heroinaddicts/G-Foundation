#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <cmath>
#include <set>

namespace {
Json::Value Header(const Json::Value& document) {
    Json::Value result(Json::objectValue);
    for(const auto& name:document.getMemberNames())if(name!="Objects")result[name]=document[name];
    return result;
}
bool NeedsDocumentValidation(const GameSproutRuntime::SceneAuthoringChange& change) {
    using GameSproutRuntime::SceneNodeEventType;
    switch(change.Type) {
    case SceneNodeEventType::Create:case SceneNodeEventType::Destroy:case SceneNodeEventType::Reparent:
    case SceneNodeEventType::SiblingOrderChanged:case SceneNodeEventType::ComponentAdded:
    case SceneNodeEventType::ComponentRemoved:return true;
    default:break;
    }
    // These components contain scene-wide object/component bindings. Keep full
    // cross-reference validation for them, not for an ordinary Camera/TRS edit.
    return change.ComponentType=="GameSproutRuntime.Animator"||change.ComponentType=="GameSproutRuntime.SkinnedMeshRenderer"||
        change.ComponentType=="GameSproutRuntime.ScriptComponent";
}
}

SceneDirtyState SceneManager::MakeDirtyBaseline(const Json::Value& document) const {
    SceneDirtyState state;auto* scene=GetActiveScene();
    state.SceneID=scene?scene->GetInstanceID():0;state.Revision=scene?scene->GetAuthoringRevision():0;
    state.NotifiedRevision=state.Revision;state.NotifiedDirty=_ActivePath.empty()&&!IsTemporaryDocumentContext();
    state.Known=true;state.BaselineRoot=Header(document);
    for(const auto& node:document["Objects"]) {
        const auto id=node["ID"].asUInt64();state.BaselineOrder.push_back(id);state.BaselineNodes.emplace(id,AssetArtifactIO::JsonText(node));
    }
    return state;
}
bool SceneManager::DirtyFromSnapshot() const {
    return GetActiveScene()&&((_ActivePath.empty()&&!IsTemporaryDocumentContext())||!_DirtyState.FullError.empty()||
        !_DirtyState.Errors.empty()||_DirtyState.RootDifferent||_DirtyState.OrderDifferent||!_DirtyState.DifferentNodes.empty());
}
bool SceneManager::RefreshDirtyDocument(const Json::Value& document,std::string& error) const {
    error.clear();_DirtyState.DifferentNodes.clear();_DirtyState.Errors.clear();_DirtyState.FullError.clear();
    std::vector<std::uint64_t> order;std::set<std::uint64_t> current;
    order.reserve(document["Objects"].size());
    for(const auto& node:document["Objects"]) {
        const auto id=node["ID"].asUInt64();order.push_back(id);current.insert(id);
        const auto baseline=_DirtyState.BaselineNodes.find(id);
        if(baseline==_DirtyState.BaselineNodes.end()||baseline->second!=AssetArtifactIO::JsonText(node))_DirtyState.DifferentNodes.insert(id);
    }
    for(const auto& entry:_DirtyState.BaselineNodes)if(!current.count(entry.first))_DirtyState.DifferentNodes.insert(entry.first);
    _DirtyState.OrderDifferent=order!=_DirtyState.BaselineOrder;
    _DirtyState.RootDifferent=Header(document)!=_DirtyState.BaselineRoot;
    auto* scene=GetActiveScene();_DirtyState.SceneID=scene?scene->GetInstanceID():0;
    _DirtyState.Revision=scene?scene->GetAuthoringRevision():0;_DirtyState.Known=true;
    return true;
}
bool SceneManager::RefreshDirtyRoot(std::string& error) const {
    auto* scene=GetActiveScene();if(!scene)return false;
    Json::Value properties;
    if(!SceneDocument::SerializeProperties(*scene,_ObjectIDs,properties,error)||!SceneDocument::ValidateProperties(properties,true,error)) {
        _DirtyState.Errors[0]=error;return false;
    }
    if(!_Annotations.empty())properties["Annotations"]=_Annotations;
    _DirtyState.Errors.erase(0);_DirtyState.RootDifferent=properties!=_DirtyState.BaselineRoot;return true;
}
bool SceneManager::SynchronizeDirtyState(std::string& error) const {
    try {
    error.clear();auto* scene=GetActiveScene();if(!scene)return true;
    const auto revision=scene->GetAuthoringRevision();
    if(_DirtyState.Known&&_DirtyState.SceneID==scene->GetInstanceID()&&revision&&revision==_DirtyState.Revision) {
        if(!_DirtyState.FullError.empty()){error=_DirtyState.FullError;return false;}
        if(!_DirtyState.Errors.empty()){error=_DirtyState.Errors.begin()->second;return false;}
        return true;
    }
    std::vector<GameSproutRuntime::SceneAuthoringChange> changes;
    // Invalid world matrices can depend on an ancestor or descendant other than
    // the node that first exposed them. Recovery is exceptional: revalidate the
    // full graph once when another authored change follows an error.
    bool complete=_DirtyState.Known&&_DirtyState.SceneID==scene->GetInstanceID()&&_DirtyState.FullError.empty()&&_DirtyState.Errors.empty()&&
        revision&&scene->GetAuthoringChanges(_DirtyState.Revision,changes);
    if(complete)for(const auto& change:changes)if(NeedsDocumentValidation(change)){complete=false;break;}
    if(!complete) {
        Json::Value document;std::string text;
        if(!Capture(document,text,error)) {
            _DirtyState.FullError=error;_DirtyState.SceneID=scene->GetInstanceID();_DirtyState.Revision=revision;_DirtyState.Known=true;return false;
        }
        return RefreshDirtyDocument(document,error);
    }
    std::map<std::uint64_t,bool> targets;
    for(const auto& change:changes)
        targets[change.ObjectID]=targets[change.ObjectID]||change.ComponentType=="GameSproutRuntime.Transform";
    for(const auto& [id,transformChanged]:targets) {
        auto* object=Runtime()->QueryGameObject(id);
        if(!object||object->GetScene()!=scene) {
            Json::Value document;std::string text;
            if(!Capture(document,text,error)){_DirtyState.FullError=error;return false;}
            return RefreshDirtyDocument(document,error);
        }
        if(object==scene) {RefreshDirtyRoot(error);continue;}
        const auto file=_ObjectIDs.RuntimeToFile.find(id);
        if(file==_ObjectIDs.RuntimeToFile.end()) {
            Json::Value document;std::string text;
            if(!Capture(document,text,error)){_DirtyState.FullError=error;return false;}
            return RefreshDirtyDocument(document,error);
        }
        Json::Value properties;
        bool valid=SceneDocument::SerializeProperties(*object,_ObjectIDs,properties,error)&&SceneDocument::ValidateProperties(properties,false,error);
        if(valid&&transformChanged) {
            std::vector<GameSproutRuntime::IGameObject*> descendants;
            for(std::size_t index=0;index<object->GetChildCount();++index)descendants.push_back(object->GetChild(index));
            for(std::size_t index=0;valid&&index<descendants.size();++index) {
                auto* child=descendants[index];const auto world=child->GetTransform()->GetWorldMatrix();
                for(const float value:world.Data)if(!std::isfinite(value)){error="World transform overflows for object "+child->GetName();valid=false;break;}
                for(std::size_t childIndex=0;childIndex<child->GetChildCount();++childIndex)descendants.push_back(child->GetChild(childIndex));
            }
        }
        if(!valid){_DirtyState.Errors[file->second]=error;continue;}
        _DirtyState.Errors.erase(file->second);
        const auto baseline=_DirtyState.BaselineNodes.find(file->second);
        if(baseline!=_DirtyState.BaselineNodes.end()&&baseline->second==AssetArtifactIO::JsonText(properties))_DirtyState.DifferentNodes.erase(file->second);
        else _DirtyState.DifferentNodes.insert(file->second);
    }
    _DirtyState.Revision=revision;
    if(!_DirtyState.Errors.empty()){error=_DirtyState.Errors.begin()->second;return false;}
    return true;
    } catch(const std::exception& exception){error=exception.what();}
      catch(...){error="Cannot update authored scene snapshot";}
    _DirtyState.FullError=error;return false;
}
