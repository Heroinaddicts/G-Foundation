#ifndef __GameSproutRuntimeApi_h__
#define __GameSproutRuntimeApi_h__

#include "RuntimeTypeVisibility.h"
#include <string>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <typeinfo>
#include <vector>
#include "Math/Bounds.h"
#include "Math/Color.h"
#include "Math/CoordinateSystem.h"
#include "Math/Plane.h"
#include "Math/Projection.h"
#include "Math/Ray.h"
#include "Math/Rect.h"
#include "Math/Viewport.h"
#include "ResourceReference.h"
#include "RuntimeInputApi.h"
#include "RuntimePrefabApi.h"
#include "RuntimeProjectSettings.h"
#include "RuntimeStaticFlags.h"


namespace GameSproutRuntime {
    struct AnimationNotification;
    struct RootMotionNotification;
    class IRuntimeRenderer;
    class IRuntimeViewport;
    struct NativeWindow;
    class IRuntimeResourceProvider;
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IScriptComponent;
    struct ScriptTypeRegistration;
    struct ScriptTypeInfo;
    struct BuiltinComponentInfo;
    struct PhysicsQuerySettings;
    struct PhysicsRaycastHit;
    struct EnvironmentLightingSettings;
    class IObject {
    public:
        virtual ~IObject() {}
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IGameObject;
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IScene;
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE ITransformComponent;
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE ICameraComponent;
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IGameSproutRuntime;

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IComponent : IObject {
    public:
        virtual ~IComponent() {}
        IComponent(IGameObject * const gameObject) : _GameObject(gameObject) {}

        // 返回注册的接口类型，不返回 ImageComponent 等内部实现类的类型。
        virtual const std::type_info& GetComponentType() const = 0;
        // 场景序列化使用稳定标识，例如 GameSproutRuntime.Image；不能保存 RTTI/hash_code。
        virtual const char* GetTypeName() const = 0;
        // Owner-local live identity, assigned by Runtime, monotonic and never
        // reused. Scene/Prefab file IDs are separate and restored by ObjectDocument.
        virtual std::uint64_t GetComponentInstanceID() const { return _ComponentInstanceID; }
        IGameObject* GetGameObject() const { return _GameObject; }
        // 借用所属对象，组件不可迁移到另一个对象。
        IGameObject * const _GameObject;
        IComponent(const IComponent&) = delete;
        IComponent& operator=(const IComponent&) = delete;

    protected:
        // 生命周期仅由 Runtime 调用：登记归属后 OnCreate 一次，销毁前 OnRelease 一次。
        // OnInitialized 在本批节点、组件、属性及引用装配完成、结构修改提交后调用一次。
        // 它保证同批组件的数据可访问，不保证其他组件已执行自己的 OnInitialized。
        // 尚未初始化成功的组件不会进入 Update / FixedUpdate；资源引用就绪不等于 GPU 上传完成。
        // 回调中请求的结构修改与销毁延迟至当前回调/遍历结束，不直接 delete。
        virtual void OnCreate(IGameSproutRuntime * const runtime) {}
        virtual void OnInitialized(IGameSproutRuntime * const runtime) {}
        virtual void Update(IGameSproutRuntime * const runtime, float deltaSeconds) {} // 秒。
        virtual void FixedUpdate(IGameSproutRuntime * const runtime, float fixedDeltaSeconds) {}
        virtual void OnRelease(IGameSproutRuntime * const runtime) {}

    private:
        std::uint64_t _ComponentInstanceID = 0;
        friend class GameObjectState;
        friend class Runtime;
        friend class ScriptComponent;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IScript : public IComponent {
    public:
        virtual ~IScript() {}
        using IComponent::IComponent;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IGameObject : IObject {
    public:
        virtual ~IGameObject() {}

        // 全部在所属宿主线程调用；返回指针由 Runtime 持有，外部不自行 delete。
        virtual std::uint64_t GetInstanceID() const = 0; // 所属 Runtime 内唯一、不复用；不是资产 GUID。
        virtual const std::string& GetName() const = 0;
        virtual void SetName(const std::string& name) = 0;
        // One Layer and one semantic Tag per object. Parent changes do not inherit either value.
        virtual std::uint8_t GetLayer() const = 0;
        virtual bool SetLayer(std::uint8_t layer) = 0; // 0..31, unnamed slots are still valid.
        virtual std::uint64_t GetTagID() const = 0;
        virtual const std::string& GetTag() const = 0; // Borrowed until project settings change.
        virtual bool SetTagID(std::uint64_t tagID) = 0;
        virtual bool SetTag(const std::string& name) = 0; // Undefined names fail without mutation.
        virtual bool CompareTag(const std::string& name) const = 0;
        // Authored metadata only. Known flags can be restored even when no
        // precomputation consumer is available; Editor capability checks are separate.
        virtual StaticFlags GetStaticFlags() const = 0;
        virtual bool SetStaticFlags(StaticFlags flags) = 0; // Unknown bits fail without mutation.
        bool IsStatic() const {return GetStaticFlags()!=StaticFlags::None;}

        // 未挂入存活场景的临时对象返回 nullptr；裸指针不会延长其帧尾寿命。
        virtual IScene* GetScene() const = 0;
        virtual IGameObject * Parent() const = 0;
        // nullptr 表示解除挂接；Scene 根不能设置父节点。结构回调内延迟提交。
        virtual bool SetParent(IGameObject* parent, bool keepWorldTransform = true) = 0;
        virtual std::size_t GetChildCount() const = 0;
        virtual IGameObject* GetChild(std::size_t index) const = 0; // 越界返回 nullptr。

        // 只查直接子节点；重名返回层级顺序中第一个。
        virtual IGameObject * FindChild(const std::string & name) const = 0;

        typedef std::function<void(IGameObject * const child)> fForeachChildCallback;
        // 空回调无操作，遍历期间的层级修改/销毁延迟到遍历结束。
        virtual void ForeachChild(const fForeachChildCallback & callback) const = 0;

        // 可先组装无场景子树，再整体挂入场景；已归属不同场景的对象不能直接互挂。
        // 拒绝空子节点、自身、祖先和场景根。
        // 保留世界变换时，若新父矩阵不可逆或结果不能表示为 TRS，失败且不改原层级。
        virtual bool AddChild(IGameObject* child, bool keepWorldTransform = true) = 0;
        // 真正解除直接父子关系，进入帧尾待回收集合；本帧重新挂接可保留。
        virtual bool RemoveChild(IGameObject* child, bool keepWorldTransform = true) = 0;
        // 调整同一父节点内的顺序，超界放到末尾，不改变父节点或任何变换。
        // 场景根、待销毁对象以及 Runtime 更新/遍历/事件回调期间拒绝；不延迟排队。
        // 已在目标位置返回 true 且不发通知。
        virtual bool SetSiblingIndex(std::size_t index) = 0;
        virtual void SetActive(bool active) = 0;
        virtual bool IsActiveSelf() const = 0;
        virtual bool IsActiveInHierarchy() const = 0;
        // 固有且唯一的 Transform，不允许移除；场景根为固定单位变换。
        virtual ITransformComponent* GetTransform() const = 0;

        // C 是已注册的公开接口；工厂在 Runtime 内部创建实现，不在模板中 new。
        // 多实例 Collider 每次创建新组件；其他内置接口重复添加返回已有对象。
        // 用户脚本同类型只能挂一个，重复添加返回 nullptr。
        template<typename C>
        C* AddComponent() {
            static_assert(std::is_convertible_v<C*, IComponent*>, "C must publicly derive from IComponent");
            return static_cast<C*>(AddComponent(typeid(C)));
        }
        template<typename C>
        C* GetComponent() const {
            static_assert(std::is_convertible_v<C*, IComponent*>, "C must publicly derive from IComponent");
            return static_cast<C*>(GetComponent(typeid(C)));
        }
        template<typename C>
        std::vector<C*> GetComponents() const {
            static_assert(std::is_convertible_v<C*, IComponent*>, "C must publicly derive from IComponent");
            std::vector<C*> result;
            for (std::size_t i=0;i<GetComponentCount();++i)
                if (auto* value=dynamic_cast<C*>(GetComponentAt(i))) result.push_back(value);
            return result;
        }
        template<typename C>
        bool RemoveComponent() {
            static_assert(std::is_convertible_v<C*, IComponent*>, "C must publicly derive from IComponent");
            return RemoveComponent(typeid(C));
        }
        virtual std::size_t GetComponentCount() const = 0;
        virtual IComponent* GetComponentAt(std::size_t index) const = 0; // 越界返回 nullptr。
        using fForeachComponentCallback = std::function<void(IComponent* component)>;
        virtual void ForeachComponent(const fForeachComponentCallback& callback) const = 0;
        // Inspector undo restores a component's original slot without rebuilding the object.
        virtual bool SetComponentIndex(IComponent*, std::size_t) { return false; }
        // Remove this exact live component; required for repeated Collider types.
        virtual bool RemoveComponentInstance(std::uint64_t) { return false; }
        IGameObject(const IGameObject&) = delete;
        IGameObject& operator=(const IGameObject&) = delete;

    protected:
        IGameObject() = default;
        // 按 type_info 值相等匹配，不比较 type_info 地址。未注册接口返回 nullptr；
        // 构造或 OnCreate 失败由 Runtime 回滚并清理，不留半挂载组件。
        virtual IComponent* AddComponent(const std::type_info& type) = 0;
        virtual IComponent* GetComponent(const std::type_info& type) const = 0;
        virtual bool RemoveComponent(const std::type_info& type) = 0;
    };

    struct SceneObjectIdentity {
        std::uint64_t ObjectID = 0; // InstanceID in this Runtime only.
        std::uint64_t FileID = 0; // Stable within this scene document; root is 0.
    };

    struct SceneComponentIdentity {
        IComponent* Component = nullptr;
        std::uint64_t FileID = 0; // Document-wide nonzero FileID, distinct from lifetime ID.
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IScene : public IGameObject {
    public:
        virtual ~IScene() {}
        // 场景是特殊根：Parent=nullptr，GetScene 返回自身，不允许挂到其他节点下。
        // parent=nullptr 表示场景根，拒绝其他场景的 parent。
        virtual IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) = 0;
        // 销毁对象和整棵子树；场景根通过 IGameSproutRuntime::DestroyScene 销毁。
        virtual bool DestroyGameObject(IGameObject* gameObject) = 0;
        // Host-owned simulation gate, default true. It pauses Update/FixedUpdate
        // without changing hierarchy activation, visibility or renderability.
        // This transient host state is not part of a scene authoring document.
        virtual void SetSimulationEnabled(bool enabled) { (void)enabled; }
        virtual bool IsSimulationEnabled() const { return true; }
        // 默认游戏相机必须属于本场景；nullptr 清除。Scene 观察相机由 Editor 独立管理。
        virtual bool SetMainCamera(ICameraComponent* camera) = 0;
        virtual ICameraComponent* GetMainCamera() const = 0;
        // Host supplies stable source identities during scene assembly/save.
        // FileID 0 denotes this scene root; ordinary nodes require nonzero IDs.
        virtual bool BindObjectIdentity(IGameObject*, std::uint64_t, std::string& error) {
            error="Scene object identities are unavailable";return false;
        }
        // Atomic publication; validation failure leaves every identity unchanged.
        virtual bool SetComponentIdentities(const std::vector<SceneComponentIdentity>&, std::string& error) {
            error="Scene component identities are unavailable";return false;
        }
        virtual bool BindObjectIdentities(const std::vector<SceneObjectIdentity>&, std::string& error) {
            error="Scene object identities are unavailable";return false;
        }
        virtual const EnvironmentLightingSettings& GetEnvironmentLighting() const = 0;
        // Scene-owned state; invalid input leaves the complete state unchanged.
        virtual bool SetEnvironmentLighting(const EnvironmentLightingSettings&) = 0;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE ITransformComponent : public IComponent {
    public:
        virtual Vector3 GetLocalPosition() const = 0; // 米。
        virtual void SetLocalPosition(Vector3 position) = 0;
        virtual Quaternion GetLocalRotation() const = 0;
        // 自动归一化；零四元数/非有限值失败，保留原值。
        virtual bool SetLocalRotation(Quaternion rotation) = 0;
        virtual Vector3 GetLocalScale() const = 0; // 默认 (1,1,1)，零缩放可能造成不可逆。
        virtual void SetLocalScale(Vector3 scale) = 0;
        virtual bool IsAnimationDriven() const { return false; }
        virtual Mat4 GetLocalMatrix() const = 0; // T * R * S。
        virtual Mat4 GetWorldMatrix() const = 0; // ParentWorld * Local。
    protected:
        using IComponent::IComponent;
    };

    // UI 布局与固有 Transform 共存，由布局系统驱动变换。
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IRectTransformComponent : public IComponent {
    public:
        virtual Vector2 GetAnchorMin() const = 0; // 左下 (0,0)、右上 (1,1)。
        virtual Vector2 GetAnchorMax() const = 0;
        virtual void SetAnchors(Vector2 minimum, Vector2 maximum) = 0;
        virtual Vector2 GetPivot() const = 0; // 默认 (0.5,0.5)。
        virtual void SetPivot(Vector2 pivot) = 0;
        virtual Vector2 GetAnchoredPosition() const = 0; // Canvas 布局单位，X 右、Y 上。
        virtual void SetAnchoredPosition(Vector2 position) = 0;
        virtual Vector2 GetSizeDelta() const = 0; // 相对于锚点跨度的尺寸增量。
        virtual void SetSizeDelta(Vector2 size) = 0;
    protected:
        using IComponent::IComponent;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IImageComponent : public IComponent {
    public:
        virtual ResourceReference GetTexture() const = 0;
        virtual void SetTexture(const ResourceReference& texture) = 0;
        virtual Color GetColor() const = 0; // 实例颜色乘数，不修改共享贴图。
        virtual void SetColor(Color color) = 0;
        virtual Rect GetUVRect() const = 0; // 默认 Position(0,0)、Size(1,1)。
        virtual void SetUVRect(const Rect& rect) = 0;
    protected:
        using IComponent::IComponent;
    };

    enum class CanvasRenderMode { ScreenSpace, WorldSpace };
    class GAMESPROUT_RUNTIME_PUBLIC_TYPE ICanvasComponent : public IComponent {
    public:
        virtual CanvasRenderMode GetRenderMode() const = 0;
        virtual void SetRenderMode(CanvasRenderMode mode) = 0;
        virtual Vector2 GetReferenceResolution() const = 0; // 布局参考尺寸，默认 (1920,1080)。
        virtual bool SetReferenceResolution(Vector2 resolution) = 0; // 有限且为正。
        virtual float GetPixelsPerUnit() const = 0; // 默认 100 个布局单位对应世界 1 米。
        virtual bool SetPixelsPerUnit(float value) = 0; // 有限且为正，仅用于世界空间 Canvas。
        // 屏幕空间 Canvas 由视图提供输出像素矩形；世界空间 Canvas 使用自身 Transform。
        // 布局坐标 X 右/Y 上，转屏幕坐标时由 UI 系统统一翻转 Y。
    protected:
        using IComponent::IComponent;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IMeshComponent : public IComponent {
    public:
        virtual ResourceReference GetMesh() const = 0;
        virtual void SetMesh(const ResourceReference& mesh) = 0;
        virtual Bounds GetLocalBounds() const = 0; // 最近加载网格的局部包围盒。
    protected:
        using IComponent::IComponent;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IMeshRendererComponent : public IComponent {
    public:
        virtual std::size_t GetMaterialCount() const = 0;
        virtual ResourceReference GetMaterial(std::size_t slot) const = 0;
        virtual bool SetMaterial(std::size_t slot, const ResourceReference& material) = 0;
        // 整表替换；空数组清除所有槽位，最多 1024 槽。失败保留原值。
        virtual bool SetMaterials(const std::vector<ResourceReference>& materials) = 0;
        virtual void SetVisible(bool visible) = 0;
        virtual bool IsVisible() const = 0;
    protected:
        using IComponent::IComponent;
    };

    enum class CameraProjection { Perspective, Orthographic };
    struct CameraSettings {
        CameraProjection Projection = CameraProjection::Perspective;
        float VerticalFovRadians = DegreesToRadians(60.0f);
        float OrthographicHeight = 10.0f; // 米，完整垂直跨度。
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        std::uint32_t CullingMask = 0xFFFFFFFFu;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE ICameraComponent : public IComponent {
    public:
        virtual const CameraSettings& GetSettings() const = 0;
        virtual bool SetSettings(const CameraSettings& settings) = 0; // 校验失败不修改。
        virtual bool GetViewMatrix(Mat4& output) const = 0; // 世界矩阵不可逆时失败。
        virtual bool GetProjectionMatrix(float aspect, Mat4& output) const = 0;
    protected:
        using IComponent::IComponent;
    };

    enum class SceneNodeEventType {
        Create, Destroy, Reparent, Rename, ActiveChanged,
        ComponentAdded, ComponentRemoved, ComponentChanged, OperationFailed, MainCameraChanged,
        TagChanged, LayerChanged, StaticFlagsChanged,
        SiblingOrderChanged, // ParentID/OldParentID 相同，仅兄弟顺序改变。
        EnvironmentLightingChanged
    };
    struct SceneNodeEvent {
        SceneNodeEventType Type = SceneNodeEventType::Create;
        std::uint64_t SceneID = 0;
        std::uint64_t ObjectID = 0;
        std::uint64_t ParentID = 0;
        std::uint64_t OldParentID = 0;
        std::string Name;
        std::string ComponentType;
        std::string Error;
    };
    struct AnimatorIKNotification;
    class IRuntimeListener {
    public:
        virtual ~IRuntimeListener() = default;
        virtual void OnAnimatorIK(const AnimatorIKNotification&) {}
        virtual void OnSceneNodeEvent(const SceneNodeEvent& event) = 0;
        // Host-thread, end-of-Update delivery. Unregister before destroying the listener.
        virtual void OnAnimationEvent(const AnimationNotification&) {}
        // Return true to take over movement (e.g. GPhysics); builtin ApplyToObject then does not apply it.
        virtual bool OnRootMotion(const RootMotionNotification&) { return false; }
    };

    // Host-thread subscription to one live object's value changes. Runtime stores
    // identities only; unregister before destroying the observer. Delivery is at
    // a safe boundary under IsExecuting(), so edits must wait until it returns.
    class IRuntimePropertyObserver {
    public:
        virtual ~IRuntimePropertyObserver() = default;
        virtual void OnRuntimeObjectPropertiesChanged(std::uint64_t sceneID,std::uint64_t objectID) = 0;
    };

    class GAMESPROUT_RUNTIME_PUBLIC_TYPE IGameSproutRuntime {
    public:
        virtual ~IGameSproutRuntime() = default;
        // Runtime owns and shares the GPU device. Hosts pass only their native window.
        // Delete returned viewports before this Runtime and the resource provider.
        virtual IRuntimeViewport* CreateViewport(const NativeWindow&, IRuntimeResourceProvider*, std::string& error) = 0;
        virtual IRuntimeViewport* CreateOffscreenRenderer(std::uint32_t width, std::uint32_t height,
            IRuntimeResourceProvider*, std::string& error) = 0;
        virtual IScene* CreateScene(const std::string& name) = 0;
        virtual bool DestroyScene(IScene* scene) = 0;
        virtual std::size_t GetSceneCount() const = 0;
        virtual IScene* GetScene(std::size_t index) const = 0;
        virtual IGameObject* QueryGameObject(std::uint64_t instanceID) const = 0;
        virtual bool RegisterObjectPropertyObserver(std::uint64_t,IRuntimePropertyObserver*) {return false;}
        virtual void UnregisterObjectPropertyObserver(std::uint64_t,IRuntimePropertyObserver*) {}
        // Synchronize changed physics configuration/poses without time integration,
        // script callbacks, or collision/trigger delivery. Call outside Runtime work.
        virtual bool SynchronizePhysics(std::string& error) {error="Physics synchronization is unavailable";return false;}
        virtual const RuntimeProjectSettings& GetProjectSettings() const = 0;
        // Host-thread safe point. Validate and replace all definitions atomically; reject deletion
        // of a Tag still referenced by any live node, including inactive/detached nodes.
        virtual bool CanApplyProjectSettings(const RuntimeProjectSettings&,std::string& error) const = 0;
        virtual bool SetProjectSettings(const RuntimeProjectSettings&,std::string& error) = 0;
        // Active attached ordinary objects only. Optional scene must belong to this Runtime.
        // Undefined Tag names return no results; the one-object result has the smallest live ID.
        virtual IGameObject* FindGameObjectWithTag(const std::string&,const IScene* scene=nullptr) const = 0;
        virtual std::vector<IGameObject*> FindGameObjectsWithTag(const std::string&,const IScene* scene=nullptr) const = 0;
        virtual std::vector<BuiltinComponentInfo> GetBuiltinComponentTypes() const;
        virtual IComponent* AddBuiltinComponent(IGameObject*, const std::string&, std::string& error);
        virtual bool RemoveBuiltinComponent(IGameObject*, const std::string&, std::string& error);
        virtual bool RegisterScriptType(const ScriptTypeRegistration&, std::string& error);
        // Host-thread, stopped execution. Validate and publish one module's
        // complete type catalog and authored field definitions atomically.
        virtual bool ReplaceScriptTypes(const std::vector<std::string>& removeTypes,
            const std::vector<ScriptTypeRegistration>& addTypes, std::string& error);
        virtual bool UnregisterScriptType(const std::string& typeID, std::string& error);
        virtual std::vector<ScriptTypeInfo> GetScriptTypes() const;
        // Scene-local authored identities. The Runtime performs all native
        // reference fixups; Editor/Player hosts only supply values and IDs.
        virtual bool BindSceneObjectIdentity(IGameObject*, std::uint64_t, std::string& error) {
            error="Scene object identities are unavailable";return false;
        }
        virtual IGameObject* QuerySceneObjectByFileID(const IScene*, std::uint64_t) const {return nullptr;}
        virtual bool GetSceneObjectFileID(const IGameObject*, std::uint64_t&) const {return false;}
        virtual bool BindSceneObjectIdentities(IScene*, const std::vector<SceneObjectIdentity>&, std::string& error) {
            error="Scene object identities are unavailable";return false;
        }
        // Complete scene snapshot; atomically replaces all previous component bindings.
        virtual bool SetSceneComponentIdentities(IScene*, const std::vector<SceneComponentIdentity>&, std::string& error) {
            error="Scene component identities are unavailable";return false;
        }
        virtual IComponent* QuerySceneComponentByFileID(const IScene*, std::uint64_t) const {return nullptr;}
        virtual bool GetSceneComponentFileID(const IComponent*, std::uint64_t&) const {return false;}
        virtual bool IsComponentType(const IComponent*, const std::string&) const {return false;}
        virtual IScriptComponent* AddScriptComponent(IGameObject*, const std::string& typeID,
            const ResourceReference& source, std::uint64_t instanceID, std::string& error);
        virtual bool RemoveScriptComponent(IGameObject*, std::uint64_t instanceID, std::string& error);
        virtual bool SetScriptExecutionEnabled(bool enabled, std::string& error);
        virtual bool IsScriptExecutionEnabled() const { return false; }
        // Host-thread gameplay session. Prepare every initially active script before
        // OnCreate, commit their structural changes, then OnInitialized before ticks.
        // A failed begin releases all created instances and leaves execution disabled.
        virtual bool BeginExecution(std::string& error) { return SetScriptExecutionEnabled(true,error); }
        virtual bool EndExecution(std::string& error) { return SetScriptExecutionEnabled(false,error); }
        // Dispatch pending value events without evaluating components or advancing time.
        virtual void ProcessPendingEvents() { Update(0); }
        // Host thread. Borrowed state is stable until the next input/Host-frame
        // operation; gameplay scripts query it during their FixedUpdate/Update.
        virtual const RuntimeInputState& GetInputState() const {static const RuntimeInputState empty;return empty;}
        // Only focused, unpaused gameplay accepts key/mouse input. Repeat keydown
        // keeps Held without repeating Pressed; FocusLost releases all held keys.
        virtual bool SubmitInputEvent(const RuntimeInputEvent&) {return false;}
        virtual void ResetInput() {}
        // Host only: call once after the complete fixed+variable gameplay frame,
        // not per viewport or per fixed substep. Held persists; edge/delta reset.
        virtual void CompleteInputFrame() {}
        // Host only: pause/resume reset input so stale presses cannot cross modes.
        virtual void SetInputEnabled(bool) {}
        // 主线程订阅，不持有监听者；销毁监听者前注销。事件在 Update 尾部派发。
        // 不回放注册前的事件；先订阅，再查询节点树。回调参数只在本次调用有效。
        virtual bool RegisterListener(IRuntimeListener* listener) = 0;
        virtual void UnregisterListener(IRuntimeListener* listener) = 0;
        // Update、Render、节点/组件遍历及事件回调内部返回 true；此时禁止同步加载场景。
        virtual bool IsExecuting() const = 0;
        // Borrowed prepared resource service; set before creating/loading animation instances.
        // The service must outlive this Runtime or be detached before it is destroyed.
        virtual void SetResourceProvider(IRuntimeResourceProvider*) {}
        virtual IRuntimeResourceProvider* GetResourceProvider() const { return nullptr; }
        // Owner-thread physics query. Synchronizes pending author transforms
        // without advancing time; succeeds only when a Collider is hit.
        virtual bool Raycast(IScene*,const Ray&,float maxDistance,PhysicsRaycastHit&,
            const PhysicsQuerySettings&,std::string& error) {error="Physics queries are unavailable";return false;}
        virtual IGameObject* InstantiateModel(IScene*, const ResourceReference&, IGameObject*, std::string& error) { error="Model instantiation is unavailable";return nullptr; }
        // Atomic template assembly; output mappings change only on success.
        // parent=nullptr denotes the scene root. All internal authored references
        // are remapped to fresh scene identities before any initialization runs.
        virtual IGameObject* InstantiatePrefabData(IScene*, const PrefabData&, IGameObject*,
            std::vector<PrefabObjectMapping>&, std::string& error) { error="Prefab instantiation is unavailable";return nullptr; }
        virtual IGameObject* InstantiatePrefab(IScene*, const ResourceReference&, IGameObject*,
            std::vector<PrefabObjectMapping>&, std::string& error) { error="Prefab instantiation is unavailable";return nullptr; }
        virtual void Update(float deltaSeconds) = 0;
        // Explicit host fixed-step dispatch; editing and animation preview do not run player code.
        virtual void FixedUpdate(float fixedDeltaSeconds) {} // 有限且非负，单位秒。
        // 旧的无 View 帧入口不猜测窗口或游戏主相机。3D 绘制使用 CreateViewport/CreateOffscreenRenderer + RenderView。
        virtual void Render() = 0;
        // 不指定 parent 时创建可配置的临时对象；本帧挂入场景后才能初始化，
        // 否则 EndFrame 回收。也可显式指定 parent（包括临时父节点）。
        // 跨 Runtime 或已销毁父节点返回 nullptr。
        // Runtime owns the result and its intrinsic Transform. Components and
        // local TRS can be configured immediately, including in OnInitialized.
        // During callbacks/traversal, Parent/Children are committed at the safe
        // operation boundary; they do not change while that callback is running.
        virtual IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) { return nullptr; }
        // Host-thread assembly scope, nestable. Structural APIs still commit normally;
        // initialization, simulation and rendering wait until the outer scope closes.
        // Use this around scene/model loading or configuration across several calls.
        virtual bool BeginConstruction(std::string& error) = 0;
        // Close the scope even on initialization failure. On rollback destroy partial
        // objects before closing it. A callback's scope commits after its safe boundary.
        virtual bool EndConstruction(std::string& error) = 0;
        // Host 每帧调用一次，放在所有逻辑步和 Scene/Game 视口处理之后。
        // 提交结构并回收最终无场景归属的子树；不推进时间，构建范围内暂缓回收。
        virtual bool EndFrame(std::string& error) = 0;
    };

}

#include "RuntimeAnimationApi.h"
#include "RuntimeScriptApi.h"
#include "RuntimePhysicsApi.h"
#include "RuntimeLightingApi.h"

#endif //__GameSproutRuntimeApi_h__
