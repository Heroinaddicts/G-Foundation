#pragma once
#include "RuntimeRenderer.h"

namespace GameSproutRuntime {
// GPU resources belong to a Runtime/device/provider, not to a camera or window.
// No scene nodes, camera data, shadow maps or instance streams live here.
class RenderResourceCache final {
public:
    RenderResourceCache(GRHI::IDevice& device,IRuntimeResourceProvider& provider)
        :Device(device),Provider(provider) {}
    ~RenderResourceCache();
    RenderResourceCache(const RenderResourceCache&)=delete;
    RenderResourceCache& operator=(const RenderResourceCache&)=delete;
    void Synchronize();

    GRHI::IDevice& Device;
    IRuntimeResourceProvider& Provider;
    std::size_t Leases=0;
    std::uint64_t Generation=0;
    std::map<Renderer::ResourceKey,Renderer::MeshGPU> Meshes;
    std::map<Renderer::ResourceKey,Renderer::TextureGPU> Textures;
    std::map<Renderer::ResourceKey,Renderer::Material> Materials;
    std::map<Renderer::ResourceKey,Renderer::ShaderGPU> UserShaders;
    std::map<Renderer::ShaderPipelineKey,GRHI::IGraphicsPipeline*> ShaderPipelines;
    std::map<Renderer::PipelineKey,GRHI::IGraphicsPipeline*> Pipelines;
    GRHI::IShader* Shaders[19]{};
    std::map<GRHI::TextureFormat,GRHI::IGraphicsPipeline*> SkyPipelines;
    GRHI::ITexture* White=nullptr;
    GRHI::ISampler* DefaultSampler=nullptr;
    GRHI::IBuffer* Grid=nullptr;
    std::uint32_t GridVertices=0;
private:
    void ClearPreparedResources();
    std::uint64_t _Revision=0;
    bool _RevisionKnown=false;
};
}
