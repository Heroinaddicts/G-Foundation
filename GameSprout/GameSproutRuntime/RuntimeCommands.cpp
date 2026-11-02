#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "RuntimeMath.h"
#include "Components/ComponentFactory.h"
#include <algorithm>

namespace GameSproutRuntime {
void Runtime::EndOperation() noexcept {
    if (_Depth) --_Depth;
    if (!_Depth && !_Flushing) FlushCommands();
}
bool Runtime::CanReparent(GameObjectState& child,GameObjectState* parent,bool keepWorld,Mat4* local) const {
    if(child.IsSceneRoot() || &child==parent) return false;
    if(parent && child.SceneOwner && parent->SceneOwner && child.SceneOwner!=parent->SceneOwner) return false;
    if(parent && parent->SceneOwner) {
        std::vector<const GameObjectState*> subtree{&child};
        for(std::size_t index=0;index<subtree.size();++index) {
            const auto identity=_ObjectFileIDs.find(subtree[index]->ID);
            if(identity!=_ObjectFileIDs.end() && identity->second.SceneID!=parent->SceneOwner->GetInstanceID()) return false;
            subtree.insert(subtree.end(),subtree[index]->Children.begin(),subtree[index]->Children.end());
        }
    }
    for (GameObjectState* ancestor=parent;ancestor;ancestor=ancestor->Parent) if (ancestor==&child) return false;
    if (!keepWorld) return true;
    try {
        Mat4 inverse;
        if(parent && !RuntimeMath::TryInverseAffine(parent->Object->GetTransform()->GetWorldMatrix(),inverse)) return false;
        const Mat4 candidate=inverse*child.Object->GetTransform()->GetWorldMatrix();
        Vector3 position,scale;Quaternion rotation;
        if(!RuntimeMath::TryDecomposeTRS(candidate,position,rotation,scale)) return false;
        if(local) *local=candidate;
        return true;
    } catch (...) { return false; }
}
bool Runtime::RequestReparent(GameObjectState* child,GameObjectState* parent,bool keepWorld) {
    if(!IsUsable(child) || (parent && !IsUsable(parent))) return false;
    const bool pendingMove=std::any_of(_Commands.begin(),_Commands.end(),[&](const Command& command){
        return command.ObjectID==child->ID && (command.Type==CommandType::Reparent || command.Type==CommandType::Attach);
    });
    if(child->IsSceneRoot()) return false;
    if(child->Parent==parent && !pendingMove) return true;
    if(!CanReparent(*child,parent,keepWorld)) return false;
    _Commands.push_back({CommandType::Reparent,child->ID,parent?parent->ID:0,keepWorld});
    return true;
}
bool Runtime::ReparentNow(GameObjectState& child,GameObjectState* parent,bool keepWorld) {
    if(!IsUsable(&child) || (parent && !IsUsable(parent))) return false;
    if(child.Parent==parent) {child.PendingAttach=false;return true;}
    Mat4 local;
    if(!CanReparent(child,parent,keepWorld,&local)) return false;
    if(parent) parent->Children.reserve(parent->Children.size()+1);
    std::vector<GameObjectState*> subtree{&child};
    for(std::size_t index=0;index<subtree.size();++index)
        subtree.insert(subtree.end(),subtree[index]->Children.begin(),subtree[index]->Children.end());
    const bool wasInScene=IsInScene(&child);
    const bool willBeInScene=parent && IsInScene(parent);
    Scene* oldScene=child.SceneOwner;
    Scene* newScene=parent?parent->SceneOwner:nullptr;
    if(!willBeInScene) for(auto* node:subtree) _DetachedObjects.insert(node->ID);
    if(keepWorld && !SetLocalTransform(child.Object->GetTransform(),local)) return false;
    const auto oldParent=child.Parent?child.Parent->ID:0;
    if(child.Parent) std::erase(child.Parent->Children,&child);
    if(wasInScene && !willBeInScene) for(auto* node:subtree) {
        InvalidateScriptObjectReferences(node->Object);
        Notify(*node,SceneNodeEventType::Destroy);
        if(oldScene) oldScene->ClearMainCameraForNode(node->ID);
    }
    if(parent) parent->Children.push_back(&child);
    child.Parent=parent;child.PendingAttach=false;child.Initializing=false;
    for(auto* node:subtree) {
        node->SceneOwner=newScene;
        if(willBeInScene) _DetachedObjects.erase(node->ID);
    }
    if(willBeInScene) {
        if(!wasInScene) for(auto* node:subtree) Notify(*node,SceneNodeEventType::Create);
        else Notify(child,SceneNodeEventType::Reparent,"",oldParent);
        QueueObjectInitialization(child,true);
        RefreshScriptObjectReferences();
    }
    return true;
}
bool Runtime::RequestDestroy(GameObjectState* object,bool scene) {
    if(!object || _ShuttingDown || object->Destroying) return false;
    if(object->PendingDestroy) return true;
    if(!IsUsable(object) || object->IsSceneRoot()!=scene) return false;
    _Commands.push_back({scene?CommandType::DestroyScene:CommandType::DestroyObject,object->ID});
    object->PendingDestroy=true;
    return true;
}
bool Runtime::RequestRemoveComponent(GameObjectState* object,IComponent* component,std::uint64_t serial) {
    if(!IsUsable(object)) return false;
    Command command{CommandType::RemoveComponent,object->ID};
    command.Component=component;command.Serial=serial;
    _Commands.push_back(command);
    return true;
}
void Runtime::DestroyNow(GameObjectState& state) {
    // 关闭场景时也回收尚未挂载的对象；不分配额外遍历内存。
    if(state.IsSceneRoot()) {
        for (;;) {
            GameObjectState* other=nullptr;
            for(const auto& entry:_Nodes) if(entry.second!=&state && entry.second->SceneOwner==state.SceneOwner) { other=entry.second;break; }
            if(!other) break;
            DestroyNow(*other);
        }
    }
    GameObjectState* current=&state;
    for (;;) {
        if(!current->Children.empty()) {current=current->Children.back();continue;}
        const bool finished=current==&state;
        GameObjectState* parent=current->Parent;
        IGameObject* object=current->Object;
        if(current->SceneOwner) current->SceneOwner->ClearMainCameraForNode(current->ID);
        Notify(*current,SceneNodeEventType::Destroy);
        current->Destroying=true;
        InvalidateScriptObjectReferences(object);
        ForgetSceneObjectIdentity(current->ID);
        ForgetObjectAnimationBindings(current->ID);
        _PropertyObservations.erase(current->ID);
        current->ReleaseComponents();
        if(parent) std::erase(parent->Children,current);
        if(current->IsSceneRoot()) std::erase(_Scenes,current->SceneOwner);
        ForgetObjectTag(*current);
        _DetachedObjects.erase(current->ID);
        _Nodes.erase(current->ID);
        _ByPointer.erase(object);
        delete object;
        if(finished) break;
        current=parent;
    }
}
void Runtime::FlushCommands() noexcept {
    if(_Flushing || _Depth) return;
    _Flushing=true;
    while(!_Commands.empty()) {
        const Command command=_Commands.front();_Commands.pop_front();
        GameObjectState* object=Find(command.ObjectID);
        if(!object) continue;
        try {
            bool success=true;
            switch(command.Type) {
            case CommandType::Attach: {
                GameObjectState* parent=Find(command.ParentID);
                success=parent && ReparentNow(*object,parent,false);
                if(!success) {object->PendingAttach=false;object->SceneOwner=nullptr;}
                break;
            }
            case CommandType::Reparent: {
                GameObjectState* parent=Find(command.ParentID);
                success=(!command.ParentID || parent) && ReparentNow(*object,parent,command.KeepWorld);
                break;
            }
            case CommandType::DestroyObject:
            case CommandType::DestroyScene: DestroyNow(*object);break;
            case CommandType::RemoveComponent: object->RemoveComponentNow(command.Component,command.Serial);break;
            }
            if(!success && Find(command.ObjectID)) Notify(*object,SceneNodeEventType::OperationFailed);
        } catch (...) {
            if(GameObjectState* remaining=Find(command.ObjectID)) {
                remaining->PendingDestroy=false;
                Notify(*remaining,SceneNodeEventType::OperationFailed);
                if(command.Type==CommandType::Attach) DestroyNow(*remaining);
            }
        }
    }
    _Flushing=false;
}
}
