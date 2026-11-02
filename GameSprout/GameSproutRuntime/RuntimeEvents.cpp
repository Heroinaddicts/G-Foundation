#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include <limits>

namespace GameSproutRuntime {
void Runtime::Notify(GameObjectState& object,SceneNodeEventType type,const char* component,std::uint64_t oldParent) {
    if(_ShuttingDown || object.Initializing || !object.SceneOwner) return;
    if(type==SceneNodeEventType::Destroy) {
        // Explicit destruction has already set PendingDestroy, and detachment
        // emits the old scene's removal before clearing its parent/scene data.
        // Follow committed parents without treating those destruction flags as
        // membership loss; a merely intended scene on PendingAttach is not enough.
        const GameObjectState* root=&object;
        for(;;) {
            if(root->PendingAttach) return;
            if(!root->Parent) break;
            root=root->Parent;
        }
        if(root->Object!=object.SceneOwner || Find(root->Object)!=root) return;
    } else if(!IsInScene(&object)) return;
    MarkObjectPropertiesChanged(object.ID);
    // 通知失败不能破坏已提交的对象/组件状态，事件本身不借用节点或组件指针。
    try {
        SceneNodeEvent event;
        event.Type=type;
        event.SceneID=object.SceneOwner->GetInstanceID();
        event.ObjectID=object.ID;
        event.ParentID=object.Parent?object.Parent->ID:0;
        event.OldParentID=oldParent;
        event.Name=object.Name;
        event.ComponentType=component;
        if(type==SceneNodeEventType::OperationFailed) event.Error="Deferred operation no longer satisfies hierarchy/transform constraints";
        _Events.push_back({++_EventSequence,std::move(event)});
    } catch (...) {}
}
bool Runtime::RegisterListener(IRuntimeListener* listener) {
    if(!listener || _ShuttingDown || _NextListener==std::numeric_limits<std::uint64_t>::max()) return false;
    for(const auto& entry:_Listeners) if(entry.second.Pointer==listener) return false;
    _Listeners.emplace(++_NextListener,Listener{listener,_EventSequence,_AnimationSequence});
    return true;
}
void Runtime::UnregisterListener(IRuntimeListener* listener) {
    for(auto it=_Listeners.begin();it!=_Listeners.end();) {
        if(it->second.Pointer==listener) it=_Listeners.erase(it);else ++it;
    }
}
void Runtime::DispatchEvents() {
    // 固定本轮批次；回调产生的新通知保留到下一次 Update，避免无限递归派发。
    std::deque<EventRecord> batch;
    batch.swap(_Events);
    for(const auto& record:batch) {
        std::vector<std::uint64_t> listeners;
        listeners.reserve(_Listeners.size());
        for(const auto& entry:_Listeners) listeners.push_back(entry.first);
        RuntimeOperation operation(*this);
        for(auto id:listeners) {
            const auto found=_Listeners.find(id);
            if(found==_Listeners.end() || record.Sequence<=found->second.AfterSequence) continue;
            try {found->second.Pointer->OnSceneNodeEvent(record.Event);} catch (...) {}
        }
    }
}
}
