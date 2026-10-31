#include "ShaderSDL.h"
#include "DeviceSDL.h"

namespace GRHI {
ShaderSDL::ShaderSDL(DeviceSDL* owner, uint64_t id, SDL_GPUShader* native, ShaderStage stage, uint32_t samplers, uint32_t uniforms)
    : IShader(id), Owner(owner), Native(native), Stage(stage), SamplerCount(samplers), UniformCount(uniforms) {
    Owner->RegisterResource(static_cast<IShader*>(this),this,id,ResourceKindSDL::Shader);
}

ShaderSDL::~ShaderSDL() {
    Release();
}

void ShaderSDL::Release() {
    Owner->RemoveResource(static_cast<IShader*>(this));
    if (Native) {
        SDL_ReleaseGPUShader(Owner->Native(), Native);
        Native = nullptr;
    }
}

ShaderStage ShaderSDL::GetStage() const { return Stage; }

static SDL_GPUShaderFormat ShaderFormat(ShaderCodeFormat format) {
    switch (format) {
    case ShaderCodeFormat::SPIRV: return SDL_GPU_SHADERFORMAT_SPIRV;
    case ShaderCodeFormat::DXBC: return SDL_GPU_SHADERFORMAT_DXBC;
    case ShaderCodeFormat::DXIL: return SDL_GPU_SHADERFORMAT_DXIL;
    case ShaderCodeFormat::MSL: return SDL_GPU_SHADERFORMAT_MSL;
    case ShaderCodeFormat::Metallib: return SDL_GPU_SHADERFORMAT_METALLIB;
    default: return SDL_GPU_SHADERFORMAT_INVALID;
    }
}
IShader* DeviceSDL::CreateShader(const ShaderDesc& desc) {
    ClearError();
    if (!_Device || !desc.Codes || !desc.CodeCount || static_cast<unsigned>(desc.Stage) > 1) {
        Fail("Invalid shader description or released device."); return nullptr;
    }
    const auto supported = SDL_GetGPUShaderFormats(_Device);
    for (size_t i = 0; i < desc.CodeCount; ++i) {
        const auto& code = desc.Codes[i];
        const auto format = ShaderFormat(code.Format);
        if (!(supported & format)) continue;
        if (!code.Data || !code.DataSize || !code.EntryPoint || !*code.EntryPoint || code.SamplerCount > 16 || code.UniformBufferCount > 4 ||
            (desc.Stage == ShaderStage::Vertex && code.SamplerCount) ||
            (format == SDL_GPU_SHADERFORMAT_MSL && static_cast<const char*>(code.Data)[code.DataSize - 1] != '\0')) {
            Fail("Invalid shader code, entry point or sampler count."); return nullptr;
        }
        SDL_GPUShaderCreateInfo info{};
        info.code = static_cast<const Uint8*>(code.Data); info.code_size = code.DataSize;
        info.entrypoint = code.EntryPoint; info.format = format;
        info.stage = desc.Stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = code.SamplerCount;
        info.num_uniform_buffers = code.UniformBufferCount;
        auto* native = SDL_CreateGPUShader(_Device, &info);
        if (!native) { FailSDL(); return nullptr; }
        try { return new ShaderSDL(this, NextID(), native, desc.Stage, code.SamplerCount, code.UniformBufferCount); }
        catch (...) { SDL_ReleaseGPUShader(_Device, native); throw; }
    }
    Fail("No shader code format is compatible with this device."); return nullptr;
}
}
