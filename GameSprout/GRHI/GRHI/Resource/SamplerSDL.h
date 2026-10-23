#pragma once
#include "GRHIApi.h"

struct SDL_GPUSampler;

namespace GRHI {
class DeviceSDL;
// Owns the native resource; the Device must outlive this wrapper.
class SamplerSDL final : public ISampler {
public:
    SamplerSDL(DeviceSDL* owner, uint64_t id, SDL_GPUSampler* native);
    ~SamplerSDL() override;
    void Release() override;
    DeviceSDL* const Owner;
    SDL_GPUSampler* Native;
};
}
