#pragma once
#include "GRHIApi.h"

struct SDL_GPUGraphicsPipeline;

namespace GRHI {
class DeviceSDL;
// Owns the native resource; the Device must outlive this wrapper.
class PipelineSDL final : public IGraphicsPipeline {
public:
    PipelineSDL(DeviceSDL* owner, uint64_t id, SDL_GPUGraphicsPipeline* native, TextureFormat format, uint32_t samplers);
    ~PipelineSDL() override;
    void Release() override;
    TextureFormat GetColorFormat() const override;
    DeviceSDL* const Owner;
    SDL_GPUGraphicsPipeline* Native;
    const TextureFormat Format;
    const uint32_t SamplerCount;
};
}
