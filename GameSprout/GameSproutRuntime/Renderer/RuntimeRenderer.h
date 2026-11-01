#pragma once
#include "Api/RuntimeRenderApi.h"
#include "RuntimeShaders.h"
#include "GRHIApi.h"
#include <map>
#include <tuple>

namespace GameSproutRuntime {
class Runtime;
class Renderer final : public IRuntimeViewport {
public:
    Renderer(Runtime& runtime,GRHI::IDevice& device,IRuntimeResourceProvider& resources);
    ~Renderer() override;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool Render(const RenderView& view) override;
    bool ReadPixels(std::vector<std::uint8_t>&,std::string&) override;
    bool Resize(std::uint32_t,std::uint32_t,std::string&) override;
    void BindSurface(GRHI::ISurface* surface) {_Surface=surface;}

    const std::string& GetLastError() const override {return _Error;}
    const RenderStatistics& GetStatistics() const override {return _Statistics;}
    bool GetRenderedCamera(RenderedCamera& camera) const override;
    bool Raycast(IScene* scene,const Ray& worldRay,SceneRaycastHit& hit,std::uint32_t cullingMask=~0u) override;
    bool GetWorldBounds(IScene* scene,std::uint64_t objectID,Bounds& bounds) override;
    bool AppendSelectionOutline(IScene* scene,std::uint64_t objectID,DebugDrawList& draw,
        const Color& tint,bool depthTest) override;
private:
    struct ResourceKey {
        std::string Guid;std::uint64_t LocalID=0;
        explicit ResourceKey(const ResourceReference& reference):Guid(reference.Guid),LocalID(reference.LocalID) {}
        bool operator<(const ResourceKey& other) const {return std::tie(Guid,LocalID)<std::tie(other.Guid,other.LocalID);}
    };
    struct MeshGPU {
        struct SkinVertex { std::uint32_t Bones[4]{}; float Weights[4]{}; };
        MeshRenderData Data;
        std::vector<Vector3> Positions;
        std::vector<SkinVertex> Skin;
        std::vector<std::uint32_t> Indices;
        GRHI::IBuffer* Buffer=nullptr;
        std::string Error;
    };
    struct TextureGPU {GRHI::ITexture* Texture=nullptr;GRHI::ISampler* Sampler=nullptr;std::string Error;};
    struct Material {MaterialRenderData Data;bool Loaded=false;std::string Error;};
    struct PipelineKey {
        GRHI::TextureFormat Format=GRHI::TextureFormat::Unknown;
        std::uint32_t Stride=0,Position=0,Normal=0;
        std::int32_t Color=-1,UV=-1,BoneIndices=-1,BoneWeights=-1;
        bool Grid=false,DoubleSided=false,Negative=false,DebugTriangles=false,DepthTest=true;
        bool operator<(const PipelineKey& other) const {
            return std::tie(Format,Stride,Position,Normal,Color,UV,BoneIndices,BoneWeights,Grid,DoubleSided,Negative,DebugTriangles,DepthTest)<
                std::tie(other.Format,other.Stride,other.Position,other.Normal,other.Color,other.UV,other.BoneIndices,other.BoneWeights,other.Grid,other.DoubleSided,other.Negative,other.DebugTriangles,other.DepthTest);
        }
    };
    struct Depth {GRHI::ITexture* Texture=nullptr;std::uint64_t LastUsed=0;};
    struct DrawObject {
        std::uint64_t ObjectID=0;
        Mat4 Model,Normal;
        ResourceReference Mesh;
        std::vector<ResourceReference> Materials;
        std::string Name;
        std::vector<Mat4> SkinMatrices;
        bool Skinned=false;
        bool Negative=false;
    };
    struct DrawUniform {Mat4 Model,ViewProjection,Normal;Color BaseColor;Vector4 TextureST{1,1,0,0};};
    // Keep this limit equal to Preview.hlsl. Uniform data is per object, never shared with the Mesh.
    static constexpr std::size_t MaxSkinBones = 128;
    struct SkinUniform { Mat4 Bones[MaxSkinBones]; };
    struct DebugVertex {Vector3 Position;Color Tint;};
    struct DebugBatch {std::vector<DebugVertex> Vertices;bool Triangles=false,DepthTest=false;};
    bool Initialize();
    bool Extract(const RenderView& view,std::vector<DrawObject>& objects);
    MeshGPU* Mesh(const ResourceReference& reference);
    TextureGPU* Texture(const ResourceReference& reference);
    Material* MaterialData(const ResourceReference& reference);
    GRHI::IGraphicsPipeline* Pipeline(const PipelineKey& key);
    GRHI::ITexture* DepthTarget(std::uint32_t width,std::uint32_t height);
    bool DrawGrid(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format);
    bool PrepareDebug(const DebugDrawList* draw,std::vector<DebugBatch>& batches);
    bool DrawDebug(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format,
                   const std::vector<DebugBatch>& batches,std::vector<GRHI::IBuffer*>& buffers);
    bool BeginQuery(IScene* scene);
    bool QueryObject(IScene* scene,std::uint64_t objectID,IGameObject*& object,MeshGPU*& mesh,Mat4& model);
    bool SkinMatrices(IGameObject& object,const MeshGPU& mesh,std::vector<Mat4>& matrices);
    bool PosedPositions(IGameObject& object,const MeshGPU& mesh,std::vector<Vector3>& positions);
    void SyncRevision();
    void ClearResources();
    void Diagnostic(const std::string& error);
    bool Fail(const std::string& error);
    std::string DeviceError(const std::string& operation) const;
    Runtime& _Runtime;
    GRHI::IDevice& _Device;
    GRHI::ISurface* _Surface=nullptr;
    GRHI::ITexture* _ColorTarget=nullptr;
    IRuntimeResourceProvider& _Resources;
    std::map<ResourceKey,MeshGPU> _Meshes;
    std::map<ResourceKey,TextureGPU> _Textures;
    std::map<ResourceKey,Material> _Materials;
    std::map<PipelineKey,GRHI::IGraphicsPipeline*> _Pipelines;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Depth> _Depths;
    GRHI::IShader* _Shaders[11]{};
    GRHI::ITexture* _White=nullptr;
    GRHI::ISampler* _DefaultSampler=nullptr;
    GRHI::IBuffer* _Grid=nullptr;
    std::uint32_t _GridVertices=0;
    std::uint64_t _Revision=0,_Frame=0;
    bool _RevisionKnown=false,_Rendering=false;
    RenderStatistics _Statistics;
    RenderedCamera _RenderedCamera;
    bool _HasRenderedCamera=false;
    std::string _Error;
};
}
