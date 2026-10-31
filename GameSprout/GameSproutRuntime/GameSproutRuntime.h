#pragma once
#include "Api/GameSproutRuntimeApi.h"
#include "Animation/AnimationResources.h"
#include <deque>
#include <map>
#include <typeindex>
#include <vector>

namespace GameSproutRuntime {
class GameObjectState;
class Scene;
class AnimatorComponent;

class Runtime final : public IGameSproutRuntime {
public:
    Runtime() = default;
    ~Runtime() override;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    IRuntimeRenderer* CreateRenderer(GRHI::IDevice* device,IRuntimeResourceProvider* resources) override;
    IScene* CreateScene(const std::string& name) override;
    bool DestroyScene(IScene* scene) override;
    std::size_t GetSceneCount() const override { return _Scenes.size(); }
    IScene* GetScene(std::size_t index) const override;
    IGameObject* QueryGameObject(std::uint64_t id) const override;
    bool RegisterListener(IRuntimeListener* listener) override;
    void UnregisterListener(IRuntimeListener* listener) override;
    bool IsExecuting() const override { return _Depth != 0 || _Updating || _Flushing; }
    void Update(float deltaSeconds) override;
    void Render() override;
    void SetResourceProvider(IRuntimeResourceProvider*) override;
    IRuntimeResourceProvider* GetResourceProvider() const override {return _AnimationResources.GetProvider();}
    IGameObject* InstantiateModel(IScene*,const ResourceReference&,IGameObject*,std::string&) override;
    AnimationResources& GetAnimationResources() {return _AnimationResources;}
    void ForgetObjectAnimationBindings(std::uint64_t objectID);
    void ForgetAnimatorReferences(std::uint64_t objectID);
    bool DispatchAnimatorIK(AnimatorComponent&,std::uint32_t);
    bool QueueAnimation(AnimatorComponent&,const AnimationNotification&);
    bool QueueRootMotion(AnimatorComponent&,const RootMotionNotification&);

    GameObjectState* Find(IGameObject* object) const;
    GameObjectState* Find(std::uint64_t id) const;
    IGameObject* CreateObject(Scene* scene, const std::string& name, IGameObject* parent);
    bool RequestDestroy(GameObjectState* object, bool scene);
    bool RequestReparent(GameObjectState* child, GameObjectState* parent, bool keepWorld);
    bool RequestRemoveComponent(GameObjectState* object, IComponent* component, std::uint64_t serial);
    bool IsUsable(const GameObjectState* object) const;
    void BeginOperation() { ++_Depth; }
    void EndOperation() noexcept;
    void Notify(GameObjectState& object, SceneNodeEventType type, const char* component = "", std::uint64_t oldParent = 0);
    bool IsShuttingDown() const { return _ShuttingDown; }

private:
    enum class CommandType { Attach, Reparent, DestroyObject, DestroyScene, RemoveComponent };
    struct Command {
        CommandType Type;
        std::uint64_t ObjectID;
        std::uint64_t ParentID = 0;
        bool KeepWorld = true;
        IComponent* Component = nullptr;
        std::uint64_t Serial = 0;
    };
    struct EventRecord { std::uint64_t Sequence; SceneNodeEvent Event; };
    struct Listener { IRuntimeListener* Pointer; std::uint64_t AfterSequence; std::uint64_t AfterAnimationSequence=0; };
    struct AnimationRecord {
        std::uint64_t Sequence=0,ObjectID=0,Serial=0,Version=0;
        bool Motion=false;
        AnimationNotification Event;
        RootMotionNotification RootMotion;
    };
    std::uint64_t NextID();
    bool RegisterNode(GameObjectState& state);
    bool ReparentNow(GameObjectState& child, GameObjectState& parent, bool keepWorld);
    bool CanReparent(GameObjectState& child, GameObjectState& parent, bool keepWorld, Mat4* local = nullptr) const;
    void DestroyNow(GameObjectState& state);
    void FlushCommands() noexcept;
    void DispatchEvents();
    void DispatchAnimationEvents();
    AnimatorComponent* ResolveAnimation(const AnimationRecord&) const;
    bool QueueAnimationRecord(AnimatorComponent&,AnimationRecord);
    void RenderWorld();
    void RenderUI();
    std::vector<Scene*> _Scenes;
    std::map<std::uint64_t,GameObjectState*> _Nodes;
    std::map<IGameObject*,GameObjectState*> _ByPointer;
    std::deque<Command> _Commands;
    std::deque<EventRecord> _Events;
    std::deque<AnimationRecord> _AnimationEvents;
    std::uint64_t _AnimationSequence=0;
    std::map<std::uint64_t,Listener> _Listeners;
    std::uint64_t _NextID = 0;
    std::uint64_t _EventSequence = 0;
    std::uint64_t _NextListener = 0;
    std::size_t _Depth = 0;
    bool _Flushing = false;
    bool _Updating = false;
    bool _ShuttingDown = false;
    AnimationResources _AnimationResources;
};

// 所有回调与树/组件遍历共用一个屏障。退出最外层后再执行结构命令。
class RuntimeOperation {
public:
    explicit RuntimeOperation(Runtime& owner) : _Owner(owner) { _Owner.BeginOperation(); }
    ~RuntimeOperation() { _Owner.EndOperation(); }
    RuntimeOperation(const RuntimeOperation&) = delete;
    RuntimeOperation& operator=(const RuntimeOperation&) = delete;
private:
    Runtime& _Owner;
};
}
