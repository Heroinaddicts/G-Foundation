#include "SceneManager.h"
#include "SceneSaveSnapshot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
Json::Value Header(const Json::Value& document) {
    Json::Value result(Json::objectValue);
    for(const auto& name:document.getMemberNames())if(name!="Objects")result[name]=document[name];
    return result;
}
bool ChangesOrder(GameSproutRuntime::SceneNodeEventType type) {
    using GameSproutRuntime::SceneNodeEventType;
    switch(type) {
    case SceneNodeEventType::Create:case SceneNodeEventType::Destroy:case SceneNodeEventType::Reparent:
    case SceneNodeEventType::SiblingOrderChanged:return true;
    default:return false;
    }
}
bool HasAnimationBindings(const Json::Value& node) {
    for(const auto& component:node["Components"])
        if(component["Type"]=="GameSproutRuntime.Animator"||component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer")return true;
    return false;
}
bool CheckAnimationTargets(GameSproutRuntime::IGameObject& object,GameSproutRuntime::IGameSproutRuntime& runtime,std::string& error) {
    const auto valid=[&](std::uint64_t id) {
        auto* target=runtime.QueryGameObject(id);
        return target&&target->GetScene()==object.GetScene()&&target!=object.GetScene()?target:nullptr;
    };
    if(const auto* animator=object.GetComponent<GameSproutRuntime::IAnimatorComponent>())
        for(const auto& binding:animator->GetNodeBindings())if(binding.ObjectID&&!valid(binding.ObjectID)) {
            error="Animator binding target is outside the scene";return false;
        }
    if(const auto* skin=object.GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>())if(const auto id=skin->GetAnimatorObjectID()) {
        auto* target=valid(id);
        if(!target||!target->GetComponent<GameSproutRuntime::IAnimatorComponent>()) {
            error="SkinnedMeshRenderer target must contain an Animator";return false;
        }
    }
    return true;
}

struct SceneRow {
    std::uint64_t ID;
    Json::Value Properties;
    std::string Text;
};
struct SceneRowBatch {
    std::vector<SceneRow> Rows;
    std::future<void> Worker;

    void Encode() {
        for(auto& row:Rows)row.Text=SceneSaveSnapshot::Row(row.Properties);
    }
    void EncodeAsync() {
        try { Worker=std::async(std::launch::async,[this]{Encode();}); }
        catch(const std::system_error&) {Encode();}
    }
    template<class Consumer>
    void Collect(Consumer& consume) {
        if(Worker.valid())Worker.get();
        for(auto& row:Rows)consume(row.ID,std::move(row.Text));
        Rows.clear();
    }
};

// Runtime values are read on the calling thread. Workers only own and format
// bounded batches of JSON. The consumer always runs on the calling thread.
template<class Consumer>
class SceneRowEncoder {
public:
    SceneRowEncoder(std::size_t rowCount,Consumer consume)
        :_Consume(std::move(consume)),_BatchCount(rowCount<2*BatchSize?0:
            std::min({_Batches.size(),rowCount/BatchSize,static_cast<std::size_t>(std::thread::hardware_concurrency())})) {}

    void Add(std::uint64_t id,Json::Value&& properties) {
        if(_BatchCount<2) {
            _Consume(id,SceneSaveSnapshot::Row(properties));
            return;
        }
        auto& batch=_Batches[_BatchIndex];
        batch.Rows.push_back({id,std::move(properties),{}});
        if(batch.Rows.size()==BatchSize) {
            batch.EncodeAsync();
            _BatchIndex=(_BatchIndex+1)%_BatchCount;
            _Batches[_BatchIndex].Collect(_Consume);
        }
    }

    void Finish() {
        _Batches[_BatchIndex].Encode();
        for(auto& batch:_Batches)batch.Collect(_Consume);
    }

private:
    static constexpr std::size_t BatchSize=256;
    Consumer _Consume;
    std::array<SceneRowBatch,3> _Batches;
    std::size_t _BatchCount;
    std::size_t _BatchIndex=0;
};
}

SceneDirtyState SceneManager::MakeDirtyBaseline(const Json::Value& document) const {
    SceneDirtyState state;auto* scene=GetActiveScene();
    state.SceneID=scene?scene->GetInstanceID():0;state.Revision=scene?scene->GetAuthoringRevision():0;
    state.NotifiedRevision=state.Revision;state.NotifiedDirty=_ActivePath.empty()&&!IsTemporaryDocumentContext();
    state.Known=true;state.BaselineRoot=Header(document);state.CurrentRoot=state.BaselineRoot;
    for(const auto& node:document["Objects"]) {
        const auto id=node["ID"].asUInt64();state.BaselineOrder.push_back(id);state.BaselineNodes.emplace(id,SceneSaveSnapshot::Row(node));
        if(HasAnimationBindings(node))state.AnimationOwners.insert(id);
    }
    state.CurrentOrder=state.BaselineOrder;
    return state;
}
bool SceneManager::DirtyFromSnapshot() const {
    return GetActiveScene()&&((_ActivePath.empty()&&!IsTemporaryDocumentContext())||!_DirtyState.FullError.empty()||
        !_DirtyState.Errors.empty()||_DirtyState.RootDifferent||_DirtyState.OrderDifferent||!_DirtyState.DifferentNodes.empty());
}
bool SceneManager::RefreshDirtyDocument(const Json::Value& document,std::string& error) const {
    error.clear();_DirtyState.DifferentNodes.clear();_DirtyState.ChangedNodes.clear();_DirtyState.Errors.clear();_DirtyState.FullError.clear();
    _DirtyState.AnimationOwners.clear();
    std::vector<std::uint64_t> order;std::set<std::uint64_t> current;
    order.reserve(document["Objects"].size());
    for(const auto& node:document["Objects"]) {
        const auto id=node["ID"].asUInt64();order.push_back(id);current.insert(id);
        if(HasAnimationBindings(node))_DirtyState.AnimationOwners.insert(id);
        const auto baseline=_DirtyState.BaselineNodes.find(id);
        auto text=SceneSaveSnapshot::Row(node);
        if(baseline==_DirtyState.BaselineNodes.end()||baseline->second!=text) {
            _DirtyState.DifferentNodes.insert(id);_DirtyState.ChangedNodes.emplace(id,std::move(text));
        }
    }
    for(const auto& entry:_DirtyState.BaselineNodes)if(!current.count(entry.first))_DirtyState.DifferentNodes.insert(entry.first);
    _DirtyState.OrderDifferent=order!=_DirtyState.BaselineOrder;
    _DirtyState.CurrentOrder=std::move(order);_DirtyState.CurrentRoot=Header(document);
    _DirtyState.RootDifferent=_DirtyState.CurrentRoot!=_DirtyState.BaselineRoot;
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
    _DirtyState.Errors.erase(0);_DirtyState.RootDifferent=properties!=_DirtyState.BaselineRoot;
    _DirtyState.CurrentRoot.swap(properties);return true;
}

bool SceneManager::PublishNewIdentities(const std::vector<GameSproutRuntime::IGameObject*>& objects,std::string& error) const {
    auto* scene=GetActiveScene();
    auto next=_ObjectIDs.Next;
    const auto allocate=[&]() {
        if(!next)throw std::runtime_error("Scene stable ID space exhausted");
        const auto id=next;
        next=id==std::numeric_limits<std::uint64_t>::max()?0:id+1;
        return id;
    };
    std::vector<GameSproutRuntime::SceneObjectIdentity> bindings;
    std::vector<GameSproutRuntime::SceneComponentBinding> components;
    std::vector<std::pair<GameSproutRuntime::IComponent*,std::uint64_t>> assigned;
    for(auto* object:objects) {
        const auto id=object->GetInstanceID();
        bool reattached=false;
        if(object!=scene&&!_ObjectIDs.RuntimeToFile.contains(id)) {
            std::uint64_t file=0;
            reattached=Runtime()->GetSceneObjectFileID(object,file)&&file;
            bindings.push_back({id,reattached?file:allocate()});
        }
        for(std::size_t index=0;index<object->GetComponentCount();++index) {
            auto* component=object->GetComponentAt(index);
            if(!component){error="Invalid Runtime component";return false;}
            if(_ObjectIDs.FindComponentFileID(component))continue;
            std::uint64_t file=0;
            if(!reattached||!Runtime()->GetSceneComponentFileID(component,file))file=allocate();
            components.push_back({id,component->GetComponentInstanceID(),file});
            assigned.emplace_back(component,file);
        }
    }
    if(bindings.empty()&&components.empty())return true;
    if(!scene->BindAuthoringIdentities(bindings,components,error))return false;
    for(const auto& binding:bindings)_ObjectIDs.RuntimeToFile.emplace(binding.ObjectID,binding.FileID);
    for(const auto& [component,file]:assigned)_ObjectIDs.BindComponentFileID(component,file);
    _ObjectIDs.Next=next;
    return true;
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
    const bool complete=_DirtyState.Known&&_DirtyState.SceneID==scene->GetInstanceID()&&_DirtyState.FullError.empty()&&_DirtyState.Errors.empty()&&
        revision&&scene->GetAuthoringChanges(_DirtyState.Revision,changes);
    if(!complete) {
        Json::Value document;
        if(!Capture(document,error)) {
            _DirtyState.FullError=error;_DirtyState.SceneID=scene->GetInstanceID();_DirtyState.Revision=revision;_DirtyState.Known=true;return false;
        }
        return RefreshDirtyDocument(document,error);
    }
    using GameSproutRuntime::SceneNodeEventType;
    std::map<std::uint64_t,bool> targets;
    bool orderChanged=false,checkSurvivingBindings=false;
    for(const auto& change:changes) {
        targets[change.ObjectID]=targets[change.ObjectID]||change.Type==SceneNodeEventType::Reparent||change.ComponentType=="GameSproutRuntime.Transform";
        orderChanged|=ChangesOrder(change.Type);
        checkSurvivingBindings|=change.Type==SceneNodeEventType::Destroy;
        if(change.Type==SceneNodeEventType::ComponentRemoved)
            if(auto* owner=Runtime()->QueryGameObject(change.ObjectID);owner&&owner->GetScene()==scene)_ObjectIDs.PruneComponentFileIDs(*owner);
    }
    if(checkSurvivingBindings)for(const auto file:_DirtyState.AnimationOwners)
        if(auto* owner=QueryObjectByFileID(file))targets.try_emplace(owner->GetInstanceID(),false);
    std::vector<GameSproutRuntime::IGameObject*> changedObjects;
    for(const auto& [id,unused]:targets)
        if(auto* object=Runtime()->QueryGameObject(id);object&&object->GetScene()==scene)changedObjects.push_back(object);
    if(!PublishNewIdentities(changedObjects,error))throw std::runtime_error(error);
    if(orderChanged) {
        // Structural events only need a lightweight DFS of IDs to maintain the
        // file's canonical order. No component values or JSON for untouched nodes.
        struct Visit {GameSproutRuntime::IGameObject* Node;std::size_t Depth;};
        std::vector<Visit> pending;
        std::vector<std::uint64_t> order;
        order.reserve(_DirtyState.CurrentOrder.size());
        for(std::size_t i=scene->GetChildCount();i>0;--i)pending.push_back({scene->GetChild(i-1),1});
        while(!pending.empty()) {
            const auto [node,depth]=pending.back();pending.pop_back();
            if(!node||node->GetScene()!=scene||depth>1024||order.size()>=100000)
                throw std::runtime_error("Invalid or oversized Runtime hierarchy");
            order.push_back(_ObjectIDs.RuntimeToFile.at(node->GetInstanceID()));
            for(std::size_t i=node->GetChildCount();i>0;--i)pending.push_back({node->GetChild(i-1),depth+1});
        }
        _DirtyState.OrderDifferent=order!=_DirtyState.BaselineOrder;
        _DirtyState.CurrentOrder.swap(order);
    }
    SceneRowEncoder rows(changedObjects.size(),[this](std::uint64_t file,std::string text) {
        const auto baseline=_DirtyState.BaselineNodes.find(file);
        if(baseline!=_DirtyState.BaselineNodes.end()&&baseline->second==text) {
            _DirtyState.DifferentNodes.erase(file);_DirtyState.ChangedNodes.erase(file);
        } else {
            _DirtyState.DifferentNodes.insert(file);_DirtyState.ChangedNodes[file]=std::move(text);
        }
    });
    for(const auto& [id,transformChanged]:targets) {
        auto* object=Runtime()->QueryGameObject(id);
        if(!object||object->GetScene()!=scene) {
            const auto found=_ObjectIDs.RuntimeToFile.find(id);
            if(found!=_ObjectIDs.RuntimeToFile.end()) {
                const auto file=found->second;
                _DirtyState.ChangedNodes.erase(file);_DirtyState.AnimationOwners.erase(file);
                if(_DirtyState.BaselineNodes.contains(file))_DirtyState.DifferentNodes.insert(file);
                else _DirtyState.DifferentNodes.erase(file);
                _ObjectIDs.ForgetObjectFileID(id);
            }
            continue;
        }
        if(object==scene)continue;
        const auto file=_ObjectIDs.RuntimeToFile.at(id);
        Json::Value properties;
        bool valid=CheckAnimationTargets(*object,*Runtime(),error)&&
            SceneDocument::SerializeProperties(*object,_ObjectIDs,properties,error)&&SceneDocument::ValidateProperties(properties,false,error);
        if(valid&&transformChanged) {
            std::vector<GameSproutRuntime::IGameObject*> descendants;
            for(std::size_t index=0;index<object->GetChildCount();++index)descendants.push_back(object->GetChild(index));
            for(std::size_t index=0;valid&&index<descendants.size();++index) {
                auto* child=descendants[index];const auto world=child->GetTransform()->GetWorldMatrix();
                for(const float value:world.Data)if(!std::isfinite(value)){error="World transform overflows for object "+child->GetName();valid=false;break;}
                for(std::size_t childIndex=0;childIndex<child->GetChildCount();++childIndex)descendants.push_back(child->GetChild(childIndex));
            }
        }
        if(!valid){_DirtyState.Errors[file]=error;continue;}
        _DirtyState.Errors.erase(file);
        if(HasAnimationBindings(properties))_DirtyState.AnimationOwners.insert(file);
        else _DirtyState.AnimationOwners.erase(file);
        rows.Add(file,std::move(properties));
    }
    rows.Finish();
    if(targets.contains(scene->GetInstanceID())||_DirtyState.CurrentRoot["NextObjectID"].asUInt64()!=_ObjectIDs.Next)
        RefreshDirtyRoot(error);
    if(IsTemporaryDocumentContext()&&_DocumentSession->ContentRootFileID&&orderChanged) {
        auto* content=QueryObjectByFileID(_DocumentSession->ContentRootFileID);
        if(!content||scene->GetChildCount()!=1||scene->GetChild(0)!=content)
            throw std::runtime_error("临时文档的所有节点必须位于内容根内部。");
    }
    _DirtyState.Revision=revision;
    if(!_DirtyState.Errors.empty()){error=_DirtyState.Errors.begin()->second;return false;}
    return true;
    } catch(const std::exception& exception){error=exception.what();}
      catch(...){error="Cannot update authored scene snapshot";}
    _DirtyState.FullError=error;return false;
}
