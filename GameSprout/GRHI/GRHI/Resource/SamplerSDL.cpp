#include "SamplerSDL.h"
#include "DeviceSDL.h"

namespace GRHI {
SamplerSDL::SamplerSDL(DeviceSDL* owner, uint64_t id, SDL_GPUSampler* native)
    : ISampler(id), Owner(owner), Native(native) { Owner->RegisterResource(static_cast<ISampler*>(this),this,id,ResourceKindSDL::Sampler); }

SamplerSDL::~SamplerSDL() {
    Release();
}

void SamplerSDL::Release() {
    Owner->RemoveResource(static_cast<ISampler*>(this));
    if (Native) {
        SDL_ReleaseGPUSampler(Owner->Native(), Native);
        Native = nullptr;
    }
}

ISampler* DeviceSDL::CreateSampler(const SamplerDesc& desc) {
    ClearError();
    if (!_Device || static_cast<unsigned>(desc.MinFilter) > 1 || static_cast<unsigned>(desc.MagFilter) > 1 ||
        static_cast<unsigned>(desc.AddressU) > 2 || static_cast<unsigned>(desc.AddressV) > 2) {
        Fail("Invalid sampler description or released device."); return nullptr;
    }
    const SDL_GPUSamplerAddressMode addresses[] = {SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, SDL_GPU_SAMPLERADDRESSMODE_REPEAT, SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT};
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = desc.MinFilter == FilterMode::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mag_filter = desc.MagFilter == FilterMode::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = addresses[static_cast<unsigned>(desc.AddressU)];
    info.address_mode_v = addresses[static_cast<unsigned>(desc.AddressV)];
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    auto* native = SDL_CreateGPUSampler(_Device, &info);
    if (!native) { FailSDL(); return nullptr; }
    try { return new SamplerSDL(this, NextID(), native); }
    catch (...) { SDL_ReleaseGPUSampler(_Device, native); throw; }
}
}
