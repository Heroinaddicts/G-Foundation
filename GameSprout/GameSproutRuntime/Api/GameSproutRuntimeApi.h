#ifndef __GameSproutRuntimeApi_h__
#define __GameSproutRuntimeApi_h__

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

namespace GRHI { class IDevice; }

namespace GameSproutRuntime {
    struct AnimationNotification;
    struct RootMotionNotification;
    class IRuntimeRenderer;
    class IRuntimeResourceProvider;
    class IObject {
    public:
        virtual ~IObject() {}
    };

    class IGameObject;
    class IScene;
    class ITransformComponent;
    class ICameraComponent;

    class IComponent : IObject {
    public:
        virtual ~IComponent() {}
        IComponent(IGameObject * const gameObject) : _GameObject(gameObject) {}

        // 生命周期仅由 Runtime 调用：登记归属后 OnCreate 一次，销毁前 OnRelease 一次。
        // 回调中请求的结构修改与销毁延迟至当前回调/遍历结束，不直接 delete。
        virtual void OnCreate() {}
        virtual void Update(float deltaSeconds) { (void)deltaSeconds; } // 秒。
        virtual void OnRelease() {}

        // 返回注册的接口类型，不返回 ImageComponent 等内部实现类的类型。
        virtual const std::type_info& GetComponentType() const = 0;
        // 场景序列化使用稳定标识，例如 GameSproutRuntime.Image；不能保存 RTTI/hash_code。
        virtual const char* GetTypeName() const = 0;
        IGameObject* GetGameObject() const { return _GameObject; }
        // 借用所属对象，组件不可迁移到另一个对象。
        IGameObject * const _GameObject;
        IComponent(const IComponent&) = delete;
        IComponent& operator=(const IComponent&) = delete;
    };

    class IGameObject : IObject {
    public:
        virtual ~IGameObject() {}

        // 全部在所属宿主线程调用；返回指针由 Runtime 持有，外部不自行 delete。
        virtual std::uint64_t GetInstanceID() const = 0; // 所属 Runtime 内唯一、不复用；不是资产 GUID。
        virtual const std::string& GetName() const = 0;
        virtual void SetName(const std::string& name) = 0;
        virtual IScene* GetScene() const = 0;
        virtual IGameObject * Parent() const = 0;
        virtual std::size_t GetChildCount() const = 0;
        virtual IGameObject* GetChild(std::size_t index) const = 0; // 越界返回 nullptr。

        // 只查直接子节点；重名返回层级顺序中第一个。
        virtual IGameObject * FindChild(const std::string & name) const = 0;

        typedef std::function<void(IGameObject * const child)> fForeachChildCallback;
        // 空回调无操作，遍历期间的层级修改/销毁延迟到遍历结束。
        virtual void ForeachChild(const fForeachChildCallback & callback) const = 0;

        // 同一场景内重挂载，拒绝空指针、自身、祖先和场景根。
        // 保留世界变换时，若新父矩阵不可逆或结果不能表示为 TRS，失败且不改原层级。
        virtual bool AddChild(IGameObject* child, bool keepWorldTransform = true) = 0;
        // 仅解除直接父子关系，成为所属场景的顶层节点，不销毁；同样检查 TRS 可表示性。
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
        // 第一版同接口只允许一个组件，重复添加返回已有对象，不再触发 OnCreate。
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
        bool RemoveComponent() {
            static_assert(std::is_convertible_v<C*, IComponent*>, "C must publicly derive from IComponent");
            return RemoveComponent(typeid(C));
        }
        virtual std::size_t GetComponentCount() const = 0;
        virtual IComponent* GetComponentAt(std::size_t index) const = 0; // 越界返回 nullptr。
        using fForeachComponentCallback = std::function<void(IComponent* component)>;
        virtual void ForeachComponent(const fForeachComponentCallback& callback) const = 0;
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


    class IScene : public IGameObject {
    public:
        virtual ~IScene() {}
        // 场景是特殊根：Parent=nullptr，GetScene 返回自身，不允许挂到其他节点下。
        // parent=nullptr 表示场景根，拒绝其他场景的 parent。
        virtual IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) = 0;
        // 销毁对象和整棵子树；场景根通过 IGameSproutRuntime::DestroyScene 销毁。
        virtual bool DestroyGameObject(IGameObject* gameObject) = 0;
        // 默认游戏相机必须属于本场景；nullptr 清除。Scene 观察相机由 Editor 独立管理。
        virtual bool SetMainCamera(ICameraComponent* camera) = 0;
        virtual ICameraComponent* GetMainCamera() const = 0;
    };

    class ITransformComponent : public IComponent {
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
    class IRectTransformComponent : public IComponent {
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

    class IImageComponent : public IComponent {
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
    class ICanvasComponent : public IComponent {
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

    class IMeshComponent : public IComponent {
    public:
        virtual ResourceReference GetMesh() const = 0;
        virtual void SetMesh(const ResourceReference& mesh) = 0;
        virtual Bounds GetLocalBounds() const = 0; // 最近加载网格的局部包围盒。
    protected:
        using IComponent::IComponent;
    };

    class IMeshRendererComponent : public IComponent {
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

    class ICameraComponent : public IComponent {
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
        SiblingOrderChanged // ParentID/OldParentID 相同，仅兄弟顺序改变。
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

    class IGameSproutRuntime {
    public:
        virtual ~IGameSproutRuntime() = default;
        // 返回对象由调用者 delete；借用本 Runtime、device 和资源提供者。
        // 必须先销毁 Renderer，再销毁这三个依赖；全部在宿主线程调用。
        virtual IRuntimeRenderer* CreateRenderer(GRHI::IDevice* device, IRuntimeResourceProvider* resources) = 0;
        virtual IScene* CreateScene(const std::string& name) = 0;
        virtual bool DestroyScene(IScene* scene) = 0;
        virtual std::size_t GetSceneCount() const = 0;
        virtual IScene* GetScene(std::size_t index) const = 0;
        virtual IGameObject* QueryGameObject(std::uint64_t instanceID) const = 0;
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
        virtual IGameObject* InstantiateModel(IScene*, const ResourceReference&, IGameObject*, std::string& error) { error="Model instantiation is unavailable";return nullptr; }
        virtual void Update(float deltaSeconds) = 0; // 有限且非负，单位秒。
        // 旧的无 View 帧入口不猜测窗口或游戏主相机。3D 绘制使用 CreateRenderer + RenderView。
        virtual void Render() = 0;
    };

}

#include "RuntimeAnimationApi.h"

#endif //__GameSproutRuntimeApi_h__
