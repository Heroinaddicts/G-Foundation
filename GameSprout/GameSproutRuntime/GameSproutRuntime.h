#pragma once
#include "Api/GameSproutRuntimeApi.h"
#include "Api/RuntimePropertyObserver.h"
#include "Animation/AnimationResources.h"
#include "GRHIApi.h"
#include <deque>
#include <chrono>
#include <map>
#include <set>
#include <thread>
#include <typeindex>
#include <type_traits>
#include <vector>

namespace GameSproutRuntime {
class GameObjectState;
class Scene;
class AnimatorComponent;
class ScriptComponent;
class ButtonComponent;
class RuntimePhysicsSystem;
class RenderResourceCache;
class RenderResourceLease;

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
    bool ParkAuthoringScenes(const std::vector<std::uint64_t>&,std::string&) override;
    bool RestoreAuthoringScenes(std::string&) override;
    std::size_t GetSceneCount() const override { return _Scenes.size(); }
    IScene* GetScene(std::size_t index) const override;
    IGameObject* QueryGameObject(std::uint64_t id) const override;
    bool RegisterObjectPropertyObserver(std::uint64_t,IRuntimePropertyObserver*) override;
    void UnregisterObjectPropertyObserver(std::uint64_t,IRuntimePropertyObserver*) override;
    bool SynchronizePhysics(std::string&) override;
    MeshCollisionCooker GetMeshCollisionCooker() const override;
    void MarkObjectPropertiesChanged(std::uint64_t objectID, RuntimePropertyChange change);
    // Internal matrix cache clock. Mutations advance it synchronously, before
    // deferred Editor events, so every query observes the committed hierarchy.
    std::uint64_t GetTransformEpoch() const {return _TransformEpoch;}
    void InvalidateTransforms();
    // Shared once per host frame; rendering two views never advances this clock.
    Vector4 GetShaderTime() const;
    // Internal factory metadata: inherited IComponent no-op hooks do not need
    // a frame visit. Inaccessible/ambiguous user overrides stay conservative.
    static constexpr std::uint8_t UpdateTick=1,FixedTick=2,EarlyTick=4,LateTick=8,AllTicks=15;
    template<typename Implementation> static constexpr std::uint8_t ComponentTickMask() {
        std::uint8_t mask=0;
        if constexpr(requires { &Implementation::Update; }) {
            if constexpr(!std::is_same_v<decltype(&Implementation::Update),decltype(&IComponent::Update)>)mask|=1;
        }else mask|=1;
        if constexpr(requires { &Implementation::FixedUpdate; }) {
            if constexpr(!std::is_same_v<decltype(&Implementation::FixedUpdate),decltype(&IComponent::FixedUpdate)>)mask|=2;
        }else mask|=2;
        if constexpr(requires { &Implementation::EarlyUpdate; }) {
            if constexpr(!std::is_same_v<decltype(&Implementation::EarlyUpdate),decltype(&IComponent::EarlyUpdate)>)mask|=EarlyTick;
        }else mask|=EarlyTick;
        if constexpr(requires { &Implementation::LateUpdate; }) {
            if constexpr(!std::is_same_v<decltype(&Implementation::LateUpdate),decltype(&IComponent::LateUpdate)>)mask|=LateTick;
        }else mask|=LateTick;
        return mask;
    }
    void RegisterAnimationComponent(std::uint64_t objectID,const IComponent*);
    void UnregisterAnimationComponent(std::uint64_t objectID,const IComponent*);
    void RegisterComponentTick(std::uint64_t objectID,std::uint8_t mask);
    void UnregisterComponentTick(std::uint64_t objectID,std::uint8_t mask);
    void RegisterScriptIdentity(std::uint64_t objectID,std::uint64_t instanceID);
    void UnregisterScriptIdentity(std::uint64_t objectID,std::uint64_t instanceID);
    std::vector<std::pair<std::uint64_t,std::uint64_t>> SnapshotScriptIdentities() const;

    void CheckObservedScriptProperties(std::uint64_t objectID);
    void MarkNativeScriptPropertiesChanged() {if(!_PropertyObservations.empty())_ObservedScriptFieldsDirty=true;}
    const RuntimeProjectSettings& GetProjectSettings() const override { return _ProjectSettings; }
    bool CanApplyProjectSettings(const RuntimeProjectSettings&,std::string&) const override;
    bool SetProjectSettings(const RuntimeProjectSettings&,std::string&) override;
    IGameObject* FindGameObjectWithTag(const std::string&,const IScene* = nullptr) const override;
    std::vector<IGameObject*> FindGameObjectsWithTag(const std::string&,const IScene* = nullptr) const override;
    const std::string& TagName(std::uint64_t) const;
    bool SetObjectTag(GameObjectState&,std::uint64_t);
    void ForgetObjectTag(const GameObjectState&);
    std::vector<BuiltinComponentInfo> GetBuiltinComponentTypes() const override;
    IComponent* AddBuiltinComponent(IGameObject*,const std::string&,std::string&) override;
    bool RemoveBuiltinComponent(IGameObject*,const std::string&,std::string&) override;
    bool RegisterScriptType(const ScriptTypeRegistration&,std::string&) override;
    bool ReplaceScriptTypes(const std::vector<std::string>&,const std::vector<ScriptTypeRegistration>&,std::string&) override;
    bool ReplaceGameLogicTypes(const std::vector<std::string>&,const std::vector<ScriptTypeRegistration>&,
        const std::vector<GameModuleRegistration>&,std::string&) override;
    IModule* FindModule(const std::string&) override;
    bool UnregisterScriptType(const std::string&,std::string&) override;
    std::vector<ScriptTypeInfo> GetScriptTypes() const override;
    bool BindSceneObjectIdentity(IGameObject*,std::uint64_t,std::string&) override;
    IGameObject* QuerySceneObjectByFileID(const IScene*,std::uint64_t) const override;
    bool GetSceneObjectFileID(const IGameObject*,std::uint64_t&) const override;
    bool BindSceneObjectIdentities(IScene*,const std::vector<SceneObjectIdentity>&,std::string&) override;
    bool SetSceneComponentIdentities(IScene*,const std::vector<SceneComponentIdentity>&,std::string&) override;
    bool BindAuthoringIdentities(IScene*,const std::vector<SceneObjectIdentity>&,
        const std::vector<SceneComponentBinding>&,std::string&);
    IComponent* QuerySceneComponentByFileID(const IScene*,std::uint64_t) const override;
    bool GetSceneComponentFileID(const IComponent*,std::uint64_t&) const override;
    bool IsComponentType(const IComponent*,const std::string&) const override;
    IComponent* NormalizeComponent(const IComponent*) const;
    void InvalidateScriptComponentReferences(IComponent*);
    void InvalidateNativeScriptReferences(ScriptComponent*);
    void ForgetSceneComponentIdentity(const IComponent*);
    void InvalidateScriptObjectReferences(IGameObject*);
    void RefreshScriptObjectReferences();
    void ForgetSceneObjectIdentity(std::uint64_t);
    IScriptComponent* AddScriptComponent(IGameObject*,const std::string&,const ResourceReference&,std::uint64_t,std::string&) override;
    bool RemoveScriptComponent(IGameObject*,std::uint64_t,std::string&) override;
    bool SetScriptExecutionEnabled(bool,std::string&) override;
    bool IsScriptExecutionEnabled() const override {return _ScriptExecutionEnabled;}
    bool BeginExecution(std::string&) override;
    bool EndExecution(std::string&) override;
    ScriptComponent* ExchangeInvokingScript(ScriptComponent* value) {auto* previous=_InvokingScript;_InvokingScript=value;return previous;}
    ScriptComponent* GetInvokingScript() const {return _InvokingScript;}
    void DropPhysicsSubscriptions(std::uint64_t objectID,std::uint64_t componentID);
    void DropUISubscriptions(std::uint64_t objectID, std::uint64_t componentID) noexcept;
    bool Raycast(IScene*,const Ray&,float,PhysicsRaycastHit&,const PhysicsQuerySettings&,std::string&) override;
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
    void SetUIInputViewport(IScene*, Vector2, Vector2) override;
    std::uint64_t PickUIScreenPreview(IScene*, std::uint64_t, Vector2, Vector2, Vector2) const override;
    bool GetUIScreenPreviewGeometry(IScene*, std::uint64_t, Vector2, UIScreenPreviewGeometry&, Vector2) const override;
    const std::set<std::uint64_t>& UICanvasObjects() const { return _UICanvasObjects; }
    void RegisterUICanvas(std::uint64_t id) { _UICanvasObjects.insert(id); }
    void UnregisterUICanvas(std::uint64_t id) { _UICanvasObjects.erase(id); }
    void RegisterUIButton(ButtonComponent* button) { _UIButtons.insert(button); }
    void UnregisterUIButton(ButtonComponent* button) { _UIButtons.erase(button); }
    ButtonComponent* FindUIButton(std::uint64_t objectID, std::uint64_t componentID) const;
    const ScriptTypeRegistration* FindScriptType(const std::string&) const;
    const ScriptTypeRegistration* FindScriptType(const std::type_info&) const;
    void NotifyScriptError(IGameObject*,const std::string&);
    bool RegisterListener(IRuntimeListener* listener) override;
    void UnregisterListener(IRuntimeListener* listener) override;
    bool RegisterLogListener(IRuntimeLogListener* listener) override;
    void UnregisterLogListener(IRuntimeLogListener* listener) override;
    bool IsExecuting() const override { return _Depth != 0 || _Updating || _Flushing || _InitializingComponents || _EndingFrame; }
    void AdvanceFrame(float deltaSeconds,std::uint32_t fixedStepCount,float fixedDeltaSeconds) override;
    void Update(float deltaSeconds) override;
    void FixedUpdate(float fixedDeltaSeconds) override;
    void Render() override;
    void SetResourceProvider(IRuntimeResourceProvider*) override;
    IRuntimeResourceProvider* GetResourceProvider() const override {return _AnimationResources.GetProvider();}
    IGameObject* InstantiateModel(IScene*,const ResourceReference&,IGameObject*,std::string&) override;
    IGameObject* InstantiatePrefabData(IScene*,const PrefabData&,IGameObject*,std::vector<PrefabObjectMapping>&,std::string&) override;
    IGameObject* InstantiatePrefab(IScene*,const ResourceReference&,IGameObject*,std::vector<PrefabObjectMapping>&,std::string&) override;
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
    bool IsParked(const GameObjectState* object) const;
    void QueueComponentInitialization(GameObjectState&,std::uint64_t serial);
    void QueueObjectInitialization(GameObjectState&,bool recursive);
    void QueueScriptInitialization(ScriptComponent&);
    void BeginOperation() { ++_Depth; }
    void EndOperation() noexcept;
    void Notify(GameObjectState& object, SceneNodeEventType type, const char* component = "", std::uint64_t oldParent = 0);
    bool IsShuttingDown() const { return _ShuttingDown; }
    bool IsConstructing() const { return _ConstructionDepth!=0; }
    bool PreparePhysics(std::string& error);
    void ResetPhysics(bool releaseSystem = false);
    void InvalidatePhysicsTransform(IGameObject* object);

private:
    void NotifyPhysicsChange(const GameObjectState& object, SceneNodeEventType type, const char* component);
    void WriteLog(RuntimeLogLevel level, std::string_view message, const char* file,
                  std::uint_least32_t line) noexcept override;

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
    void RefreshObservedScriptProperties();
    void DispatchObjectPropertyChanges();
    void DispatchAnimationEvents();
    AnimatorComponent* ResolveAnimation(const AnimationRecord&) const;
    bool QueueAnimationRecord(AnimatorComponent&,AnimationRecord);
    void RenderWorld();
    void ResetUIPointer();
    void QueueUIPointer(bool down);
    void DispatchUIPointer();
    struct FrameComponent {
        std::uint64_t ObjectID=0,Serial=0;
        std::uint8_t Mask=0;
        bool Animation=false,Skipped=false;
    };
    std::vector<FrameComponent> SnapshotFrameComponents(bool fixedOnly) const;
    IComponent* ResolveFrameComponent(const FrameComponent&) const;
    void RecoverCallbackConstruction(FrameComponent&,IComponent&,std::size_t expectedDepth);
    void TickComponentPhase(std::vector<FrameComponent>&,std::uint8_t,float);
    void TickAnimationPhase(std::vector<FrameComponent>&,float);
    bool ReplaceTypeCatalog(const std::vector<std::string>&,const std::vector<ScriptTypeRegistration>&,
        const std::vector<GameModuleRegistration>*,std::string&);
    bool SetScriptExecutionState(bool,std::string&);
    bool StartGameModules(std::string&);
    void StopGameModules() noexcept;
    void TickGameModulePhase(std::uint8_t phase,float deltaSeconds);
    void NotifyGameModuleError(const std::string& module,const char* phase,const char* error) noexcept;
    void FinishTickNotifications();
    void StepPhysics(float deltaSeconds);
    bool InitializePendingComponents(std::string& error,bool allowScripts);
    void NotifyInitializationError(GameObjectState&,IComponent&,const std::string&);
    std::vector<Scene*> _Scenes;
    std::vector<Scene*> _ParkedScenes;
    std::map<std::uint64_t,GameObjectState*> _Nodes;
    struct TickCounts {std::size_t Variable=0,Fixed=0,Early=0,Late=0;};
    std::map<std::uint64_t,TickCounts> _TickObjects;
    std::map<std::uint64_t,std::size_t> _AnimationObjects;
    std::set<std::pair<std::uint64_t,std::uint64_t>> _ScriptIdentities;

    RuntimeProjectSettings _ProjectSettings;
    std::map<std::uint64_t,std::string> _TagNames{{0,"Untagged"}};
    std::map<std::string,std::uint64_t> _TagIDs{{"Untagged",0}};
    std::map<std::uint64_t,std::set<std::uint64_t>> _TaggedObjects;
    std::map<IGameObject*,GameObjectState*> _ByPointer;
    std::set<std::uint64_t> _DetachedObjects;
    std::deque<Command> _Commands;
    std::deque<InitializationRecord> _PendingInitializations;
    std::deque<InitializationRecord> _ParkedInitializations;
    std::deque<EventRecord> _Events;
    std::deque<AnimationRecord> _AnimationEvents;
    std::uint64_t _AnimationSequence=0;
    std::map<std::uint64_t,Listener> _Listeners;
    const std::thread::id _LogOwnerThread = std::this_thread::get_id();
    std::map<std::uint64_t,IRuntimeLogListener*> _LogListeners;
    std::uint64_t _NextLogListener = 0;
    bool _DispatchingLog = false;
    struct ObjectPropertyObservation {
        std::uint64_t SceneID=0;
        std::map<IRuntimePropertyObserver*,std::uint64_t> Observers;
        std::map<std::uint64_t,std::map<std::string,ScriptValue>> ScriptFields;
        RuntimePropertyChange Pending=RuntimePropertyChange::None;
    };
    std::map<std::uint64_t,ObjectPropertyObservation> _PropertyObservations;
    std::uint64_t _NextPropertyObserver=0;
    std::uint64_t _TransformEpoch=1;
    bool _ReadingObservedProperties=false;
    bool _ObservedScriptFieldsDirty=false;
    std::uint64_t _NextID = 0;
    std::uint64_t _EventSequence = 0;
    std::uint64_t _NextListener = 0;
    std::size_t _Depth = 0;
    std::size_t _ConstructionDepth = 0;
    bool _Flushing = false;
    bool _Updating = false;
    void AdvanceShaderClock(float deltaSeconds);
    void PauseShaderClock();
    double _ShaderElapsedSeconds=0;
    float _ShaderDeltaSeconds=0;
    std::chrono::steady_clock::time_point _ShaderClockSample=std::chrono::steady_clock::now();
    bool _ShuttingDown = false;
    bool _ScriptExecutionEnabled = false;
    bool _StartingScripts = false;
    bool _InitializingComponents = false;
    bool _EndingFrame = false;
    bool _InputEnabled = false;
    RuntimeInputState _Input;
    std::set<std::uint64_t> _UICanvasObjects;
    std::set<ButtonComponent*> _UIButtons;
    struct UIPointerEdge { Vector2 Position; bool Down; };
    std::vector<UIPointerEdge> _UIPointerEdges;
    std::uint64_t _UIInputSceneID = 0;
    Vector2 _UIInputSize;
    Vector2 _UIRenderSize;
    std::uint64_t _UIPressedObjectID = 0, _UIPressedComponentID = 0;
    std::map<std::string,ScriptTypeRegistration> _ScriptTypes;
    std::map<std::string,GameModuleRegistration> _GameModuleTypes;
    struct ActiveGameModule {
        const GameModuleRegistration* Registration;
        IModule* Instance=nullptr;
        bool ReleaseNeeded=false,Faulted=false;
    };
    std::vector<ActiveGameModule> _ActiveGameModules;
    struct ObjectIdentity {std::uint64_t SceneID=0,FileID=0;};
    std::map<std::uint64_t,ObjectIdentity> _ObjectFileIDs;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::uint64_t> _SceneObjectIDs;
    struct ComponentIdentity {std::uint64_t SceneID=0,FileID=0,ObjectID=0,InstanceID=0;};
    std::map<const IComponent*,ComponentIdentity> _ComponentFileIDs;
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::pair<std::uint64_t,std::uint64_t>> _SceneComponentIDs;
    AnimationResources _AnimationResources;
    GRHI::GRHIApi* _RenderApi=nullptr;
    GRHI::IDevice* _RenderDevice=nullptr;
    std::map<IRuntimeResourceProvider*,RenderResourceCache*> _RenderResourceCaches;
    friend class RenderResourceLease;
    RuntimePhysicsSystem* _PhysicsSystem=nullptr;
    ScriptComponent* _InvokingScript=nullptr;
    std::string _LastPhysicsError;
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
