#include "GameObject.h"
#include "Scene.h"
#include "Components/ComponentFactory.h"
#include <algorithm>
#include <limits>

namespace GameSproutRuntime {
GameObjectState::GameObjectState(Runtime& owner, IGameObject* object, Scene* scene, std::string name, std::uint64_t id)
    : Owner(owner), Object(object), SceneOwner(scene), ID(id), Name(std::move(name)) {}
GameObjectState::~GameObjectState() { ReleaseComponents(); }
IScene* GameObjectState::GetScene() const { return SceneOwner; }

bool GameObjectState::InitializeTransform() { return AddComponent(typeid(ITransformComponent)) != nullptr; }
void GameObjectState::ReleaseComponents() noexcept {
    Destroying = true;
    while (!Components.empty()) {
        IComponent* component=Components.back().Pointer;
        Components.pop_back();
        try { component->OnRelease(); } catch (...) {}
        delete component;
    }
    Components.clear();
}
void GameObjectState::SetName(const std::string& name) {
    if (!Owner.IsUsable(this) || Name==name) return;
    Name=name;
    Owner.Notify(*this,SceneNodeEventType::Rename);
}
void GameObjectState::SetActive(bool active) {
    if (!Owner.IsUsable(this) || Active==active) return;
    Active=active;
    Owner.Notify(*this,SceneNodeEventType::ActiveChanged);
}
bool GameObjectState::IsActiveInHierarchy() const {
    for (const GameObjectState* node=this; node; node=node->Parent) if (!node->Active || node->PendingDestroy) return false;
    return true;
}
IGameObject* GameObjectState::FindChild(const std::string& name) const {
    for (GameObjectState* child:Children) if (child->Name==name) return child->Object;
    return nullptr;
}
bool GameObjectState::SetSiblingIndex(std::size_t index) {
    // 顺序修改必须同步完成。回调中的调用明确拒绝，不把节点裸指针放进延迟队列。
    if(Owner.IsExecuting() || !Owner.IsUsable(this) || Initializing || PendingAttach ||
        !Parent || !Owner.IsUsable(Parent) || Object==GetScene()) return false;
    auto& siblings=Parent->Children;
    const auto found=std::find(siblings.begin(),siblings.end(),this);
    if(found==siblings.end()) return false;
    const std::size_t current=static_cast<std::size_t>(found-siblings.begin());
    const std::size_t target=std::min(index,siblings.size()-1);
    if(current==target) return true;
    RuntimeOperation operation(Owner);
    // 旋转指针区间，不分配/重建对象或组件，局部与世界变换均保持原值。
    if(current<target) std::rotate(siblings.begin()+current,siblings.begin()+current+1,siblings.begin()+target+1);
    else std::rotate(siblings.begin()+target,siblings.begin()+current,siblings.begin()+current+1);
    Owner.Notify(*this,SceneNodeEventType::SiblingOrderChanged,"",Parent->ID);
    return true;
}
void GameObjectState::ForeachChild(const IGameObject::fForeachChildCallback& callback) const {
    if (!callback || Destroying) return;
    RuntimeOperation operation(Owner);
    const auto snapshot=Children;
    for (GameObjectState* child:snapshot) callback(child->Object);
}
void GameObjectState::ForeachComponent(const IGameObject::fForeachComponentCallback& callback) const {
    if (!callback || Destroying) return;
    RuntimeOperation operation(Owner);
    const auto snapshot=Components;
    for (const ComponentEntry& component:snapshot) callback(component.Pointer);
}
IComponent* GameObjectState::GetComponent(const std::type_info& type) const {
    for (const auto& component:Components) if (component.Pointer->GetComponentType()==type) return component.Pointer;
    return nullptr;
}
IComponent* GameObjectState::AddComponent(const std::type_info& type) {
    if (!Owner.IsUsable(this)) return nullptr;
    if (IComponent* existing=GetComponent(type)) return existing;
    RuntimeOperation operation(Owner);
    IComponent* component=nullptr;
    bool registered=false;
    try {
        component=CreateBuiltinComponent(type,Object);
        if (!component) return nullptr;
        if (NextComponentSerial==std::numeric_limits<std::uint64_t>::max()) { delete component; return nullptr; }
        Components.push_back({component,++NextComponentSerial,false});
        registered=true;
        component->OnCreate();
        // 初始化回调可以请求移除/销毁；此时不能把即将失效的接口返回调用方。
        const auto current=std::find_if(Components.begin(),Components.end(),[&](const ComponentEntry& entry){return entry.Pointer==component;});
        if(!Owner.IsUsable(this) || current==Components.end() || current->PendingRemove) return nullptr;
        Owner.Notify(*this,SceneNodeEventType::ComponentAdded,component->GetTypeName());
        return component;
    } catch (...) {
        if (registered) {
            const auto found=std::find_if(Components.begin(),Components.end(),[&](const ComponentEntry& entry){return entry.Pointer==component;});
            if(found!=Components.end()) Components.erase(found);
            try { component->OnRelease(); } catch (...) {}
        }
        delete component;
        return nullptr;
    }
}
bool GameObjectState::RemoveComponent(const std::type_info& type) {
    if (!Owner.IsUsable(this) || type==typeid(ITransformComponent)) return false;
    RuntimeOperation operation(Owner);
    for (ComponentEntry& component:Components) {
        if (component.Pointer->GetComponentType()!=type) continue;
        if (component.PendingRemove) return true;
        if (!Owner.RequestRemoveComponent(this,component.Pointer,component.Serial)) return false;
        component.PendingRemove=true;
        return true;
    }
    return false;
}
bool GameObjectState::RemoveComponentNow(IComponent* component,std::uint64_t serial) {
    auto found=std::find_if(Components.begin(),Components.end(),[&](const ComponentEntry& entry){return entry.Pointer==component && entry.Serial==serial;});
    if (found==Components.end()) return false;
    if(component->GetComponentType()==typeid(ICameraComponent)) SceneOwner->ClearMainCameraForNode(ID);
    Owner.Notify(*this,SceneNodeEventType::ComponentRemoved,component->GetTypeName());
    // 从查询中移除后再执行释放回调，避免释放阶段再次取到正在销毁的组件。
    Components.erase(found);
    try { component->OnRelease(); } catch (...) {}
    delete component;
    return true;
}
void NotifyComponentChanged(IGameObject* owner,IComponent* component) {
    if (auto* access=dynamic_cast<ObjectAccess*>(owner)) {
        auto& state=access->State();
        if (state.Owner.IsUsable(&state)) state.Owner.Notify(state,SceneNodeEventType::ComponentChanged,component->GetTypeName());
    }
}
}
