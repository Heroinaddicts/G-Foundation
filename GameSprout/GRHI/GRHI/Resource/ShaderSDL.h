#pragma once
#include "GRHIApi.h"

struct SDL_GPUShader;

namespace GRHI {
class DeviceSDL;
// Owns the native resource; the Device must outlive this wrapper.
class ShaderSDL final : public IShader {
public:
    ShaderSDL(DeviceSDL* owner, uint64_t id, SDL_GPUShader* native, ShaderStage stage, uint32_t samplers, uint32_t uniforms);
    ~ShaderSDL() override;
    void Release() override;
    ShaderStage GetStage() const override;
    DeviceSDL* const Owner;
    SDL_GPUShader* Native;
    const ShaderStage Stage;
    const uint32_t SamplerCount;
    const uint32_t UniformCount;
};
}
