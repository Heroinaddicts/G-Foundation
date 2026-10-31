#pragma once
#include "GameSproutRuntime.h"

namespace GameSproutRuntime {
class GameObjectState {
public:
    struct ComponentEntry { IComponent* Pointer; std::uint64_t Serial; bool PendingRemove = false; };
    GameObjectState(Runtime& owner, IGameObject* object, Scene* scene, std::string name, std::uint64_t id);
    ~GameObjectState();
    GameObjectState(const GameObjectState&) = delete;
    GameObjectState& operator=(const GameObjectState&) = delete;
    bool InitializeTransform();
    void SetName(const std::string& name);
    void SetActive(bool active);
    bool IsActiveInHierarchy() const;
    IScene* GetScene() const;
    IComponent* AddComponent(const std::type_info& type);
    IComponent* GetComponent(const std::type_info& type) const;
    bool RemoveComponent(const std::type_info& type);
    bool RemoveComponentNow(IComponent* component, std::uint64_t serial);
    void ForeachChild(const IGameObject::fForeachChildCallback& callback) const;
    void ForeachComponent(const IGameObject::fForeachComponentCallback& callback) const;
    IGameObject* FindChild(const std::string& name) const;
    bool SetSiblingIndex(std::size_t index);
    void ReleaseComponents() noexcept;

    Runtime& Owner;
    IGameObject* const Object;
    Scene* SceneOwner;
    const std::uint64_t ID;
    std::string Name;
    GameObjectState* Parent = nullptr;
    std::vector<GameObjectState*> Children;
    std::vector<ComponentEntry> Components;
    std::uint64_t NextComponentSerial = 0;
    bool Active = true;
    bool PendingDestroy = false;
    bool PendingAttach = false;
    bool Destroying = false;
    bool Initializing = true;
};

class ObjectAccess {
public:
    virtual ~ObjectAccess() = default;
    virtual GameObjectState& State() const = 0;
};

// 共享实现与状态，不改变公开 IScene : IGameObject 的单继承结构。
// 普通对象只实现 IGameObject；只有 Scene 实现 IScene。
template<typename Interface>
class GameObjectBase : public Interface, public ObjectAccess {
public:
    GameObjectBase(Runtime& runtime, Scene* scene, const std::string& name, std::uint64_t id)
        : _State(runtime, this, scene, name, id) {}
    GameObjectState& State() const override { return _State; }
    std::uint64_t GetInstanceID() const override { return _State.ID; }
    const std::string& GetName() const override { return _State.Name; }
    void SetName(const std::string& name) override { _State.SetName(name); }
    IScene* GetScene() const override { return _State.GetScene(); }
    IGameObject* Parent() const override { return _State.Parent ? _State.Parent->Object : nullptr; }
    std::size_t GetChildCount() const override { return _State.Children.size(); }
    IGameObject* GetChild(std::size_t index) const override { return index < _State.Children.size() ? _State.Children[index]->Object : nullptr; }
    IGameObject* FindChild(const std::string& name) const override { return _State.FindChild(name); }
    void ForeachChild(const IGameObject::fForeachChildCallback& callback) const override { _State.ForeachChild(callback); }
    bool AddChild(IGameObject* child, bool keepWorld) override {
        RuntimeOperation operation(_State.Owner);
        return _State.Owner.RequestReparent(_State.Owner.Find(child), &_State, keepWorld);
    }
    bool RemoveChild(IGameObject* child, bool keepWorld) override {
        RuntimeOperation operation(_State.Owner);
        GameObjectState* node = _State.Owner.Find(child);
        return node && node->Parent == &_State && _State.Owner.RequestReparent(node, _State.Owner.Find(_State.GetScene()), keepWorld);
    }
    bool SetSiblingIndex(std::size_t index) override { return _State.SetSiblingIndex(index); }
    void SetActive(bool active) override { _State.SetActive(active); }
    bool IsActiveSelf() const override { return _State.Active; }
    bool IsActiveInHierarchy() const override { return _State.IsActiveInHierarchy(); }
    ITransformComponent* GetTransform() const override { return static_cast<ITransformComponent*>(_State.GetComponent(typeid(ITransformComponent))); }
    std::size_t GetComponentCount() const override { return _State.Components.size(); }
    IComponent* GetComponentAt(std::size_t index) const override { return index < _State.Components.size() ? _State.Components[index].Pointer : nullptr; }
    void ForeachComponent(const IGameObject::fForeachComponentCallback& callback) const override { _State.ForeachComponent(callback); }
protected:
    IComponent* AddComponent(const std::type_info& type) override { return _State.AddComponent(type); }
    IComponent* GetComponent(const std::type_info& type) const override { return _State.GetComponent(type); }
    bool RemoveComponent(const std::type_info& type) override { return _State.RemoveComponent(type); }
private:
    mutable GameObjectState _State;
};

class GameObject final : public GameObjectBase<IGameObject> {
public:
    GameObject(Runtime& runtime, Scene* scene, const std::string& name, std::uint64_t id)
        : GameObjectBase<IGameObject>(runtime,scene,name,id) {}
};
}
