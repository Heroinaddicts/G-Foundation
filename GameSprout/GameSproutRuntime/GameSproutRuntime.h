#pragma once
#include "Api/GameSproutRuntimeApi.h"
#include "Animation/AnimationResources.h"
#include "GRHIApi.h"
#include <deque>
#include <map>
#include <set>
#include <typeindex>
#include <vector>

namespace GameSproutRuntime {
class GameObjectState;
class Scene;
class AnimatorComponent;
class ScriptComponent;

class Runtime final : public IGameSproutRuntime {
public:
    Runtime() = default;
    ~Runtime() override;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    IRuntimeViewport* CreateViewport(const NativeWindow&,IRuntimeResourceProvider*,std::string&) override;
    IRuntimeViewport* CreateOffscreenRenderer(std::uint32_t,std::uint32_t,IRuntimeResourceProvider*,std::string&) override;
    IScene* CreateScene(const std::string& name) override;
    IGameObject* CreateGameObject(const std::string& name,IGameObject* parent=nullptr) override;
    bool DestroyScene(IScene* scene) override;
    std::size_t GetSceneCount() const override { return _Scenes.size(); }
    IScene* GetScene(std::size_t index) const override;
    IGameObject* QueryGameObject(std::uint64_t id) const override;
    std::vector<BuiltinComponentInfo> GetBuiltinComponentTypes() const override;
    IComponent* AddBuiltinComponent(IGameObject*,const std::string&,std::string&) override;
    bool RemoveBuiltinComponent(IGameObject*,const std::string&,std::string&) override;
    bool RegisterScriptType(const ScriptTypeRegistration&,std::string&) override;
    bool ReplaceScriptTypes(const std::vector<std::string>&,const std::vector<ScriptTypeRegistration>&,std::string&) override;
    bool UnregisterScriptType(const std::string&,std::string&) override;
    std::vector<ScriptTypeInfo> GetScriptTypes() const override;
    bool BindSceneObjectIdentity(IGameObject*,std::uint64_t,std::string&) override;
    IGameObject* QuerySceneObjectByFileID(const IScene*,std::uint64_t) const override;
    bool GetSceneObjectFileID(const IGameObject*,std::uint64_t&) const override;
    bool BindSceneObjectIdentities(IScene*,const std::vector<SceneObjectIdentity>&,std::string&) override;
    void InvalidateScriptObjectReferences(IGameObject*);
    void RefreshScriptObjectReferences();
    void ForgetSceneObjectIdentity(std::uint64_t);
    IScriptComponent* AddScriptComponent(IGameObject*,const std::string&,const ResourceReference&,std::uint64_t,std::string&) override;
    bool RemoveScriptComponent(IGameObject*,std::uint64_t,std::string&) override;
    bool SetScriptExecutionEnabled(bool,std::string&) override;
    bool IsScriptExecutionEnabled() const override {return _ScriptExecutionEnabled;}
    bool BeginExecution(std::string&) override;
    bool EndExecution(std::string&) override;
    void ProcessPendingEvents() override;
    bool BeginConstruction(std::string&) override;
    bool EndConstruction(std::string&) override;
    bool EndFrame(std::string&) override;
    bool IsStartingScripts() const {return _StartingScripts;}
    const RuntimeInputState& GetInputState() const override {return _Input;}
    bool SubmitInputEvent(const RuntimeInputEvent&) override;
    void ResetInput() override;
    void CompleteInputFrame() override;
    void SetInputEnabled(bool) override;
    const ScriptTypeRegistration* FindScriptType(const std::string&) const;
    const ScriptTypeRegistration* FindScriptType(const std::type_info&) const;
    void NotifyScriptError(IGameObject*,const std::string&);
    bool RegisterListener(IRuntimeListener* listener) override;
    void UnregisterListener(IRuntimeListener* listener) override;
    bool IsExecuting() const override { return _Depth != 0 || _Updating || _Flushing || _InitializingComponents || _EndingFrame; }
    void Update(float deltaSeconds) override;
    void FixedUpdate(float fixedDeltaSeconds) override;
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
    bool IsInScene(const GameObjectState* object) const;
    void QueueComponentInitialization(GameObjectState&,std::uint64_t serial);
    void QueueObjectInitialization(GameObjectState&,bool recursive);
    void QueueScriptInitialization(ScriptComponent&);
    void BeginOperation() { ++_Depth; }
    void EndOperation() noexcept;
    void Notify(GameObjectState& object, SceneNodeEventType type, const char* component = "", std::uint64_t oldParent = 0);
    bool IsShuttingDown() const { return _ShuttingDown; }
    bool IsConstructing() const { return _ConstructionDepth!=0; }

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
    struct InitializationRecord { std::uint64_t ObjectID; std::uint64_t Serial; };
    struct Listener { IRuntimeListener* Pointer; std::uint64_t AfterSequence; std::uint64_t AfterAnimationSequence=0; };
    struct AnimationRecord {
        std::uint64_t Sequence=0,ObjectID=0,Serial=0,Version=0;
        bool Motion=false;
        AnimationNotification Event;
        RootMotionNotification RootMotion;
    };
    GRHI::IDevice* RenderDevice(std::string& error);
    void ReleaseGraphics();
    std::uint64_t NextID();
    bool RegisterNode(GameObjectState& state);
    bool ReparentNow(GameObjectState& child, GameObjectState* parent, bool keepWorld);
    bool CanReparent(GameObjectState& child, GameObjectState* parent, bool keepWorld, Mat4* local = nullptr) const;
    void DestroyNow(GameObjectState& state);
    void FlushCommands() noexcept;
    void DispatchEvents();
    void DispatchAnimationEvents();
    AnimatorComponent* ResolveAnimation(const AnimationRecord&) const;
    bool QueueAnimationRecord(AnimatorComponent&,AnimationRecord);
    void RenderWorld();
    void RenderUI();
    void TickComponents(float deltaSeconds, bool fixed);
    bool InitializePendingComponents(std::string& error,bool allowScripts);
    void NotifyInitializationError(GameObjectState&,IComponent&,const std::string&);
    std::vector<Scene*> _Scenes;
    std::map<std::uint64_t,GameObjectState*> _Nodes;
    std::map<IGameObject*,GameObjectState*> _ByPointer;
    std::set<std::uint64_t> _DetachedObjects;
    std::deque<Command> _Commands;
    std::deque<InitializationRecord> _PendingInitializations;
    std::deque<EventRecord> _Events;
    std::deque<AnimationRecord> _AnimationEvents;
    std::uint64_t _AnimationSequence=0;
    std::map<std::uint64_t,Listener> _Listeners;
    std::uint64_t _NextID = 0;
    std::uint64_t _EventSequence = 0;
    std::uint64_t _NextListener = 0;
    std::size_t _Depth = 0;
    std::size_t _ConstructionDepth = 0;
    bool _Flushing = false;
    bool _Updating = false;
    bool _ShuttingDown = false;
    bool _ScriptExecutionEnabled = false;
    bool _StartingScripts = false;
    bool _InitializingComponents = false;
    bool _EndingFrame = false;
    bool _InputEnabled = false;
    RuntimeInputState _Input;
    std::map<std::string,ScriptTypeRegistration> _ScriptTypes;
    struct ObjectIdentity {std::uint64_t SceneID=0,FileID=0;};
    std::map<std::uint64_t,ObjectIdentity> _ObjectFileIDs;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::uint64_t> _SceneObjectIDs;
    AnimationResources _AnimationResources;
    GRHI::GRHIApi* _RenderApi=nullptr;
    GRHI::IDevice* _RenderDevice=nullptr;
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
