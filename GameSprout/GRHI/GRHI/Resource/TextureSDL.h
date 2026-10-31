#pragma once
#include "GRHIApi.h"

struct SDL_GPUTexture;

namespace GRHI {
class DeviceSDL;
// Owns the native resource; the Device must outlive this wrapper.
class TextureSDL final : public ITexture {
public:
    TextureSDL(DeviceSDL* owner, uint64_t id, SDL_GPUTexture* native, const TextureDesc& desc);
    ~TextureSDL() override;
    void Release() override;
    uint32_t GetWidth() const override;
    uint32_t GetHeight() const override;
    TextureFormat GetFormat() const override;
    ColorTargetView GetColorTargetView() const override;
    DepthTargetView GetDepthTargetView() const override;
    DeviceSDL* const Owner;
    SDL_GPUTexture* Native;
    const TextureDesc Desc;
    uint64_t Token = 0;
};
}
