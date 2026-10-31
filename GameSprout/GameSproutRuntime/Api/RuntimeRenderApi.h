#pragma once
#include "GameSproutRuntimeApi.h"
#include "GRHIApi.h"

namespace GameSproutRuntime {

struct MeshSection {
    std::uint32_t FirstIndex = 0;
    std::uint32_t IndexCount = 0;
    std::uint32_t MaterialSlot = 0;
};

// 已预处理的顶点与 UInt32 索引原始字节，直接上传 GRHI Buffer。
// 顶点属性均为 float32：Position/Normal 三分量、Color 四分量、UV 两分量。
struct MeshRenderData {
    std::vector<std::uint8_t> Bytes;
    std::uint32_t VertexCount = 0;
    std::uint32_t VertexStride = 0;
    std::uint32_t IndexCount = 0;
    std::uint64_t IndexOffset = 0;
    std::uint32_t PositionOffset = 0;
    std::uint32_t NormalOffset = 12;
    std::int32_t ColorOffset = -1;
    std::int32_t UVOffset = -1;
    std::vector<MeshSection> Sections;
    Bounds LocalBounds;
};

struct TextureRenderData {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    bool SRGB = true;
    // 第一版上传 mip0；此处已是紧密排列的 RGBA8，无源图片解码。
    std::vector<std::uint8_t> Pixels;
    GRHI::SamplerDesc Sampler;
};

struct MaterialRenderData {
    Color BaseColor;
    ResourceReference BaseColorTexture;
    bool DoubleSided = false;
};

class IRuntimeResourceProvider {
public:
    virtual ~IRuntimeResourceProvider() = default;
    // 返回递增修订号，产物替换/删除时令 Renderer 释放并重新查询缓存。
    virtual std::uint64_t GetRevision() const = 0;
    // 同步读取已预处理数据；失败清空输出并给出原因。不得返回源 FBX/PNG。
    virtual bool LoadMesh(const ResourceReference& reference, MeshRenderData& data, std::string& error) = 0;
    virtual bool LoadTexture(const ResourceReference& reference, TextureRenderData& data, std::string& error) = 0;
    virtual bool LoadMaterial(const ResourceReference& reference, MaterialRenderData& data, std::string& error) = 0;
};

struct RenderCamera {
    Mat4 View;
    // Renderer 按实际渲染目标像素宽高计算投影矩阵。
    CameraSettings Projection;
    Vector3 Position;
};

// 世界坐标调试几何；不进入场景、不生成 Component，也不参与场景保存。
// 不检测深度的几何最后绘制，适用于始终可见的编辑工具。
struct DebugLine {
    Vector3 From, To;
    Color Tint;
    bool DepthTest = false;
};
struct DebugTriangle {
    Vector3 A, B, C;
    Color Tint;
    bool DepthTest = false;
};
struct DebugDrawList {
    std::vector<DebugLine> Lines;
    std::vector<DebugTriangle> Triangles;
};

struct RenderView {
    IScene* Scene = nullptr;
    // 二选一；Surface 来自宿主原生窗口，ColorTarget 用于离屏绘制。
    GRHI::ISurface* Surface = nullptr;
    GRHI::ITexture* ColorTarget = nullptr;
    RenderCamera Camera;
    bool DrawGrid = true;
    Color ClearColor{0.09f, 0.10f, 0.12f, 1.0f};
    // 仅在 Render 调用期间借用；最多 1,048,576 个调试顶点。
    const DebugDrawList* DebugDraw = nullptr;
};

struct SceneRaycastHit {
    std::uint64_t ObjectID = 0;
    float Distance = 0.0f; // 世界单位；传入 Direction 会归一化。
    Vector3 Position;
    Vector3 Normal; // 世界三角形几何法线，保留镜像变换后的绕序。
    std::uint32_t TriangleIndex = 0;
};

struct RenderedCamera {
    RenderCamera Camera;
    Mat4 ViewProjection;
    std::uint32_t Width = 0, Height = 0; // 实际目标像素，不是宿主逻辑尺寸。
    std::uint64_t SceneID = 0;
};

struct RenderStatistics {
    std::uint32_t DrawCalls = 0;
    std::uint32_t Triangles = 0;
    std::uint32_t VisibleObjects = 0;
    std::uint32_t SkippedObjects = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
};

class IRuntimeRenderer {
public:
    virtual ~IRuntimeRenderer() = default;
    // 不推进游戏逻辑，不改变 Scene/MainCamera 或节点；不可在 Runtime 回调中调用。
    // 目标暂时不可用（最小化等）成功跳过；GetStatistics().Width/Height 为零。
    virtual bool Render(const RenderView& view) = 0;
    virtual const std::string& GetLastError() const = 0;
    virtual const RenderStatistics& GetStatistics() const = 0;
    // 最近成功提交帧；Render 失败/目标不可用后返回 false 并清空输出。
    virtual bool GetRenderedCamera(RenderedCamera& camera) const = 0;
    // 以下查询均在宿主线程执行，禁止在 Runtime/provider 回调或 Render 内重入。
    // 精确、双面的最近 Mesh 三角形拾取；跳过非激活/不可见/缺失资源及奇异变换。
    // 无命中返回 false 且输出清空；非法参数也返回 false，GetLastError 给出原因。
    // 当前对象统一位于第 0 层，cullingMask 的最低位控制 Mesh 查询。
    virtual bool Raycast(IScene* scene, const Ray& worldRay, SceneRaycastHit& hit,
                         std::uint32_t cullingMask = ~0u) = 0;
    // 从Mesh引用顶点的本地AABB变换得到保守世界AABB；其中心=M*localCenter。
    // 可用于聚焦非激活/不可见对象；旋转时操作中心不因重新拟合世界顶点而漂移。
    virtual bool GetWorldBounds(IScene* scene, std::uint64_t objectID, Bounds& bounds) = 0;
    // 追加所选对象的精确 Mesh 边线；边按索引去重。失败不修改已有列表。
    virtual bool AppendSelectionOutline(IScene* scene, std::uint64_t objectID, DebugDrawList& draw,
        const Color& tint = Color{1.0f, 0.65f, 0.05f, 1.0f}, bool depthTest = false) = 0;
};

}
