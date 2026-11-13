#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include <algorithm>
#include <limits>
#include <string_view>

namespace GameSproutRuntime {
namespace {
bool HasWorldRenderer(const GameObjectState& object) {
    return std::any_of(object.Components.begin(), object.Components.end(), [](const auto& component) {
        const auto& type = component.Pointer->GetComponentType();
        return type == typeid(IMeshRendererComponent) || type == typeid(ILightComponent) ||
            type == typeid(IAnimatorComponent);
    });
}

bool SubtreeHasWorldRenderer(const GameObjectState& object) {
    if (HasWorldRenderer(object)) return true;
    std::vector<const GameObjectState*> pending(object.Children.begin(), object.Children.end());
    while (!pending.empty()) {
        const auto* child = pending.back();
        pending.pop_back();
        if (HasWorldRenderer(*child)) return true;
        pending.insert(pending.end(), child->Children.begin(), child->Children.end());
    }
    return false;
}

bool AffectsWorldSnapshot(const GameObjectState& object, SceneNodeEventType type) {
    switch (type) {
    case SceneNodeEventType::Create:
    case SceneNodeEventType::Destroy:
        // Attach/detach emit per-node events, including each descendant.
    case SceneNodeEventType::LayerChanged:
    case SceneNodeEventType::StaticFlagsChanged:
        return HasWorldRenderer(object);
    case SceneNodeEventType::Reparent:
    case SceneNodeEventType::ActiveChanged:
    case SceneNodeEventType::SiblingOrderChanged:
        return SubtreeHasWorldRenderer(object);
    default:
        return true;
    }
}
}

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
    NotifyPhysicsChange(object,type,component);
    if(type!=SceneNodeEventType::Rename && type!=SceneNodeEventType::TagChanged && type!=SceneNodeEventType::OperationFailed) {
        const std::string_view componentType = component ? component : "";
        const bool transform=type==SceneNodeEventType::ComponentChanged&&componentType=="GameSproutRuntime.Transform";
        const auto* screenCanvas = transform && !_UICanvasObjects.empty() ? FindUIScreenCanvasRoot(*object.Object) : nullptr;
        if (screenCanvas && screenCanvas != object.Object)
            object.SceneOwner->InvalidateUI();
        if(componentType=="GameSproutRuntime.RectTransform" || componentType=="GameSproutRuntime.Canvas" ||
           componentType=="GameSproutRuntime.Image" || componentType=="GameSproutRuntime.Text" ||
           componentType=="GameSproutRuntime.Button")
            object.SceneOwner->InvalidateUI();
        else {
            // Keep the viewport event for editor overlays even when this edit
            // leaves the shared world geometry, lighting and shadows intact.
            bool worldChanged = screenCanvas ? SubtreeHasWorldRenderer(object) : AffectsWorldSnapshot(object, type);
            if (!worldChanged && (type == SceneNodeEventType::Create || type == SceneNodeEventType::Destroy ||
                type == SceneNodeEventType::Reparent || screenCanvas)) {
                // An animator can bind to nodes outside its own hierarchy.
                // The bound bone may only have its intrinsic Transform.
                const auto affectedBone = [&](const AnimationNodeBinding& binding) {
                    if (!screenCanvas && type != SceneNodeEventType::Reparent) return binding.ObjectID == object.ID;
                    for (auto* node = Find(binding.ObjectID); node; node = node->Parent)
                        if (node == &object) return true;
                    return false;
                };
                for (const auto& [id, count] : _AnimationObjects) {
                    const auto* node = Find(id);
                    if (!node || node->SceneOwner != object.SceneOwner) continue;
                    const auto* animator = node->Object->GetComponent<IAnimatorComponent>();
                    if (!animator) continue;
                    const auto& bindings = animator->GetNodeBindings();
                    if (std::any_of(bindings.begin(), bindings.end(), affectedBone)) {
                        worldChanged = true;
                        break;
                    }
                }
            }
            if (worldChanged)
                object.SceneOwner->InvalidateRenderSnapshot(transform ? object.ID : 0);
            else object.SceneOwner->InvalidateVisual();
            if (!_UICanvasObjects.empty() &&
                (type==SceneNodeEventType::Create || type==SceneNodeEventType::Destroy ||
                 type==SceneNodeEventType::Reparent || type==SceneNodeEventType::ActiveChanged ||
                 type==SceneNodeEventType::SiblingOrderChanged))
                object.SceneOwner->InvalidateUI();
        }
    }
    object.SceneOwner->RecordAuthoringChange(object.ID,type,component);
    MarkObjectPropertiesChanged(object.ID,RuntimePropertyChange::Authored);
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
    std::vector<std::uint64_t> nodeListeners;
    if(!batch.empty()) {
        nodeListeners.reserve(_Listeners.size());
        for(const auto& entry:_Listeners)nodeListeners.push_back(entry.first);
    }
    for(const auto& record:batch) {
        if(std::any_of(_ParkedScenes.begin(),_ParkedScenes.end(),[&](const auto* scene) {
            return scene->GetInstanceID()==record.Event.SceneID;
        }))continue;
        RuntimeOperation operation(*this);
        // A registration made by this batch starts after its newest sequence.
        // Still resolve each ID because earlier callbacks may unregister it.
        for(auto id:nodeListeners) {
            const auto found=_Listeners.find(id);
            if(found==_Listeners.end() || record.Sequence<=found->second.AfterSequence) continue;
            try {found->second.Pointer->OnSceneNodeEvent(record.Event);} catch (...) {}
        }
    }
    // Snapshot identities before calling listeners; callbacks may unsubscribe or
    // request deferred destruction. Clear first so newly invalidated poses wait
    // for the next boundary rather than recursively notifying this one.
    std::vector<std::uint64_t> visualScenes;
    for(std::size_t index=0;index<GetSceneCount();++index)
        if(auto* scene=dynamic_cast<Scene*>(GetScene(index));scene&&scene->TakeVisualInvalidation())
            visualScenes.push_back(scene->GetInstanceID());
    for(const auto sceneID:visualScenes) {
        if(!Find(sceneID))continue;
        std::vector<std::uint64_t> listeners;
        for(const auto& entry:_Listeners)listeners.push_back(entry.first);
        RuntimeOperation operation(*this);
        for(const auto id:listeners) {
            const auto found=_Listeners.find(id);
            if(found==_Listeners.end())continue;
            try {found->second.Pointer->OnSceneVisualChanged(sceneID);}catch(...) {}
        }
    }
}
}
