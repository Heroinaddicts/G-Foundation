#pragma once
#include "GRHIApi.h"
#include <array>

struct SDL_GPUGraphicsPipeline;

namespace GRHI {
class DeviceSDL;
// Owns the native resource; the Device must outlive this wrapper.
class PipelineSDL final : public IGraphicsPipeline {
public:
    PipelineSDL(DeviceSDL* owner, uint64_t id, SDL_GPUGraphicsPipeline* native, const GraphicsPipelineDesc& desc,
                uint32_t samplers,uint32_t vertexUniforms,uint32_t fragmentUniforms);
    ~PipelineSDL() override;
    void Release() override;
    TextureFormat GetColorFormat() const override;
    DeviceSDL* const Owner;
    SDL_GPUGraphicsPipeline* Native;
    const TextureFormat Format;
    const uint32_t SamplerCount;
    const uint32_t VertexUniformCount, FragmentUniformCount;
    const TextureFormat DepthFormat;
    std::vector<VertexBindingDesc> Bindings;
    std::array<uint32_t,16> AttributeEnds{};
};
}
