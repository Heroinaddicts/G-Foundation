#pragma once
#include "Api/RuntimeRenderApi.h"
#include "RuntimeShaders.h"
#include "RuntimeRenderResourceLease.h"
#include "GRHIApi.h"
#include <map>
#include <tuple>

namespace GameSproutRuntime {
class Runtime;
class RenderResourceCache;
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
    bool AppendColliderWireframes(IScene* scene,const ColliderDebugOptions& options,DebugDrawList& draw) override;
private:
    friend class RenderResourceCache;
    struct ResourceKey {
        std::string Guid;std::uint64_t LocalID=0;
        explicit ResourceKey(const ResourceReference& reference):Guid(reference.Guid),LocalID(reference.LocalID) {}
        bool operator<(const ResourceKey& other) const {return std::tie(Guid,LocalID)<std::tie(other.Guid,other.LocalID);}
    };
    struct MeshGPU {
        std::uint64_t SortRank=0;
        struct SkinVertex { std::uint32_t Bones[4]{}; float Weights[4]{}; };
        MeshRenderData Data;
        std::vector<Vector3> Positions;
        std::vector<SkinVertex> Skin;
        std::vector<std::uint32_t> Indices;
        GRHI::IBuffer* Buffer=nullptr;
        GRHI::IBuffer* ShaderVertexBuffer=nullptr; // Canonical P/N/C/UV, only allocated for user shaders.
        // A private render-only bound derived from validated vertex positions.
        // Does not replace the provider's authored/queried LocalBounds contract.
        Vector3 CullMinimum,CullMaximum;
        bool CullBoundsKnown=false;
        std::string Error;
    };
    struct TextureGPU {GRHI::ITexture* Texture=nullptr;GRHI::ISampler* Sampler=nullptr;bool SRGB=false;std::string Error;};
    struct ColliderMeshWire {
        ResourceReference Reference;
        bool Convex=false;
        Mat4 Relative;
        std::vector<std::pair<Vector3,Vector3>> Edges;
        std::string Error;
    };
    struct BoundUniform {std::uint32_t Slot=0;std::vector<std::uint8_t> Bytes;};
    struct Material {
        std::uint64_t SortRank=0;
        MaterialRenderData Data;bool Loaded=false;std::string Error;
        bool ShaderAttempted=false,ShaderReady=false;
        std::vector<BoundUniform> VertexUniforms,FragmentUniforms;
        std::vector<TextureGPU*> ShaderTextures;
    };
    struct ShaderGPU {
        ShaderRenderData Data;
        GRHI::IShader* Vertex=nullptr;
        GRHI::IShader* Fragment=nullptr;
        bool Loaded=false;
        std::string Error;
    };
    struct ShaderPipelineKey {
        std::string Guid;std::uint64_t LocalID=0;
        GRHI::TextureFormat Format=GRHI::TextureFormat::Unknown;
        bool Negative=false;
        bool operator<(const ShaderPipelineKey& other) const {
            return std::tie(Guid,LocalID,Format,Negative)<std::tie(other.Guid,other.LocalID,other.Format,other.Negative);
        }
    };
    struct PipelineKey {
        GRHI::TextureFormat Format=GRHI::TextureFormat::Unknown;
        std::uint32_t Stride=0,Position=0,Normal=0;
        std::int32_t Color=-1,UV=-1,BoneIndices=-1,BoneWeights=-1;
        bool Grid=false,DoubleSided=false,Negative=false,DebugTriangles=false,DepthTest=true,Lit=false,Instanced=false,Shadow=false;
        bool operator<(const PipelineKey& other) const {
            return std::tie(Format,Stride,Position,Normal,Color,UV,BoneIndices,BoneWeights,Grid,DoubleSided,Negative,DebugTriangles,DepthTest,Lit,Instanced,Shadow)<
                std::tie(other.Format,other.Stride,other.Position,other.Normal,other.Color,other.UV,other.BoneIndices,other.BoneWeights,other.Grid,other.DoubleSided,other.Negative,other.DebugTriangles,other.DepthTest,other.Lit,other.Instanced,other.Shadow);
        }
    };
    struct Depth {GRHI::ITexture* Texture=nullptr;std::uint64_t LastUsed=0;};
    struct DrawObject {
        std::uint64_t ObjectID=0;
        Mat4 Model,Normal;
        ResourceReference Mesh;
        std::vector<ResourceReference> Materials;
        // Renderer-owned resource pointers, invalidated with the resource
        // provider revision. No Runtime node pointers survive between frames.
        MeshGPU* Geometry=nullptr;
        std::vector<Material*> MaterialCache;
        bool BoundsInitialized=false;
        std::string Name;
        std::vector<Mat4> SkinMatrices;
        bool Skinned=false;
        bool Negative=false;
        std::uint8_t Layer=0;
    };
    struct DrawUniform {Mat4 Model,ViewProjection,Normal;Color BaseColor;Vector4 TextureST{1,1,0,0};};
    static constexpr std::size_t MaxDirectionalLights=4;
    struct DirectionalLight {Vector3 TowardsLight;Color Radiance;std::uint32_t CullingMask=0;LightShadowSettings Shadows;};
    struct LitUniform {
        Vector4 CameraPosition;
        Vector4 Directions[MaxDirectionalLights]; // W is one for an active slot.
        Color Radiances[MaxDirectionalLights];
        Vector4 Parameters; // Metallic, perceptual roughness, normal scale, has normal map.
        Color Emission;
        Vector4 NormalTextureST;
    };
    struct PreparedDraw;
    struct ShadowUniform {
        Mat4 WorldToShadow[4];
        Mat4 CameraView;
        Vector4 Splits;
        Vector4 Info; // Cascade count, strength, max distance, fade start.
        Vector4 Bias[4]; // Normalized depth bias, world normal bias, soft PCF, padding.
        Vector4 Atlas[4]; // UV scale XY and top-first tile origin ZW.
    };
    bool RenderShadows(GRHI::ICommandBuffer&,const RenderView&,float aspect);
    bool DrawShadow(GRHI::IRenderPass&,const PreparedDraw&,const Mat4&,GRHI::IBuffer*,std::uint32_t);
    struct PreparedDraw {
        const DrawObject* Object=nullptr;
        MeshGPU* Mesh=nullptr;
        MeshSection Section;
        const MaterialRenderData* Material=nullptr;
        const ResourceReference* MaterialReference=nullptr;
        TextureGPU* Texture=nullptr;
        TextureGPU* NormalTexture=nullptr;
        Renderer::Material* CachedMaterial=nullptr;
        ShaderGPU* Shader=nullptr;
        bool Transparent=false;
        float DistanceSquared=0;
        std::uint64_t SectionOrdinal=0;
    };
    struct InstanceVertex {Mat4 Model,Normal;};
    struct InstanceBatchKey {
        std::uint64_t SceneID=0,FirstObjectID=0,GroupOrdinal=0;
        ResourceReference Mesh,Material;
        std::uint32_t FirstIndex=0,IndexCount=0,Part=0,Pass=0;
        std::uint8_t Layer=0;
        bool Negative=false;
        bool operator<(const InstanceBatchKey& other) const {
            return std::tie(SceneID,FirstObjectID,GroupOrdinal,Mesh.Guid,Mesh.LocalID,Material.Guid,Material.LocalID,FirstIndex,IndexCount,Part,Pass,Layer,Negative)<
                std::tie(other.SceneID,other.FirstObjectID,other.GroupOrdinal,other.Mesh.Guid,other.Mesh.LocalID,other.Material.Guid,other.Material.LocalID,
                    other.FirstIndex,other.IndexCount,other.Part,other.Pass,other.Layer,other.Negative);
        }
    };
    struct InstanceBatch {
        GRHI::IBuffer* Buffer=nullptr;
        std::vector<InstanceVertex> Vertices;
        std::uint64_t LastUsed=0;
    };
    static constexpr std::size_t MaxBatchInstances=4096;
    static constexpr std::uint64_t MaxResidentInstanceBytes=32u*1024u*1024u;
    void SortPrepared(std::vector<PreparedDraw>&);
    bool DrawPrepared(GRHI::IRenderPass&,const std::vector<PreparedDraw>&,const RenderView&,
        const Mat4&,GRHI::TextureFormat,const std::vector<DirectionalLight>&,const void*,std::size_t);
    bool DrawBuiltin(GRHI::IRenderPass&,const PreparedDraw&,const RenderView&,const Mat4&,
        GRHI::TextureFormat,const std::vector<DirectionalLight>&,const void*,std::size_t,
        GRHI::IBuffer* instances=nullptr,std::uint32_t instanceCount=1);
    GRHI::IBuffer* InstanceBuffer(const InstanceBatchKey&,const std::vector<PreparedDraw>&,std::size_t,std::size_t);
    void PruneInstanceBuffers(std::uint64_t requiredBytes=0);
    void ClearInstanceBuffers();
    bool ExtractLights(IScene*,std::vector<DirectionalLight>&);
    LitUniform MakeLitUniform(const RenderView&,const DrawObject&,const MaterialRenderData&,const std::vector<DirectionalLight>&,bool);
    // Keep this limit equal to Preview.hlsl. Uniform data is per object, never shared with the Mesh.
    static constexpr std::size_t MaxSkinBones = 128;
    struct SkinUniform { Mat4 Bones[MaxSkinBones]; };
    struct DebugVertex {Vector3 Position;Color Tint;};
    struct DebugBatch {std::vector<DebugVertex> Vertices;bool Triangles=false,DepthTest=false;};
    bool Initialize();
    bool Extract(const RenderView& view,std::vector<DrawObject>& objects);
    MeshGPU* Mesh(const ResourceReference& reference);
    ColliderMeshWire* ColliderMesh(IColliderComponent& collider,const ResourceReference& reference,bool convex,const Mat4& relative);
    TextureGPU* Texture(const ResourceReference& reference);
    Material* MaterialData(const ResourceReference& reference);
    ShaderGPU* Shader(const ResourceReference& reference);
    bool PrepareShaderMaterial(Material& material,ShaderGPU& shader);
    bool EnsureShaderVertices(MeshGPU& mesh,const ResourceReference& reference);
    GRHI::IGraphicsPipeline* ShaderPipeline(const ResourceReference&,ShaderGPU&,GRHI::TextureFormat,bool negative);
    bool DrawShaderMaterial(GRHI::IRenderPass&,Material&,ShaderGPU&,MeshGPU&,const ResourceReference&,
        const MeshSection&,const DrawObject&,const Mat4&,GRHI::TextureFormat);
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
    RenderResourceLease _ResourceLease;
    RenderResourceCache& _ResourceCache;
    std::map<ResourceKey,MeshGPU>& _Meshes;
    std::map<std::pair<std::uint64_t,std::uint64_t>,ColliderMeshWire> _ColliderMeshes;
    std::map<ResourceKey,TextureGPU>& _Textures;
    std::map<ResourceKey,Material>& _Materials;
    std::map<ResourceKey,ShaderGPU>& _UserShaders;
    std::map<ShaderPipelineKey,GRHI::IGraphicsPipeline*>& _ShaderPipelines;
    std::map<PipelineKey,GRHI::IGraphicsPipeline*>& _Pipelines;
    std::vector<DrawObject> _SceneObjects;
    std::vector<DirectionalLight> _SceneLights;
    std::vector<PreparedDraw> _PreparedDraws;
    std::size_t _SortedMeshCount=0,_SortedMaterialCount=0;
    std::uint64_t _ObjectSceneID=0,_ObjectSceneRevision=0,_LightSceneID=0,_LightSceneRevision=0;
    std::uint32_t _ObjectCullingMask=0,_ExtractionSkipped=0;
    bool _ObjectSnapshotKnown=false,_LightSnapshotKnown=false;
    std::string _ExtractionDiagnostic,_LightDiagnostic;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Depth> _Depths;
    GRHI::IShader* (&_Shaders)[17];
    GRHI::ITexture* _ShadowAtlas=nullptr;
    GRHI::ISampler* _ShadowSampler=nullptr;
    std::uint32_t _ShadowResolution=0;
    std::size_t _ShadowLightIndex=static_cast<std::size_t>(-1);
    ShadowUniform _ShadowUniform;
    RenderCamera _ShadowCamera;
    float _ShadowAspect=0;
    std::uint64_t _ShadowSceneID=0,_ShadowSceneRevision=0;
    bool _ShadowValid=false,_ShadowRecorded=false;
    std::map<InstanceBatchKey,InstanceBatch> _InstanceBatches;
    std::vector<InstanceVertex> _InstanceScratch;
    std::uint64_t _ResidentInstanceBytes=0;
    GRHI::ITexture*& _White;
    GRHI::ISampler*& _DefaultSampler;
    GRHI::IBuffer*& _Grid;
    std::uint32_t& _GridVertices;
    std::uint64_t _ResourceGeneration=0,_Frame=0;
    bool _Rendering=false;
    RenderStatistics _Statistics;
    RenderedCamera _RenderedCamera;
    bool _HasRenderedCamera=false;
    std::string _Error;
};
}
