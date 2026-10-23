#include "PipelineSDL.h"
#include "DeviceSDL.h"
#include "ShaderSDL.h"

namespace GRHI {
PipelineSDL::PipelineSDL(DeviceSDL* owner, uint64_t id, SDL_GPUGraphicsPipeline* native, TextureFormat format, uint32_t samplers)
    : IGraphicsPipeline(id), Owner(owner), Native(native), Format(format), SamplerCount(samplers) {}

PipelineSDL::~PipelineSDL() {
    Release();
}

void PipelineSDL::Release() {
    if (Native) {
        SDL_ReleaseGPUGraphicsPipeline(Owner->Native(), Native);
        Native = nullptr;
    }
}

TextureFormat PipelineSDL::GetColorFormat() const { return Format; }

IGraphicsPipeline* DeviceSDL::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    ClearError();
    auto* vertex = dynamic_cast<ShaderSDL*>(desc.VertexShader);
    auto* fragment = dynamic_cast<ShaderSDL*>(desc.FragmentShader);
    const auto format = ToSDL(desc.ColorFormat);
    if (!_Device || !vertex || !fragment || vertex->Owner != this || fragment->Owner != this ||
        !vertex->Native || !fragment->Native || vertex->Stage != ShaderStage::Vertex || fragment->Stage != ShaderStage::Fragment ||
        format == SDL_GPU_TEXTUREFORMAT_INVALID || static_cast<unsigned>(desc.Primitive) > 4 ||
        static_cast<unsigned>(desc.Cull) > 2 || static_cast<unsigned>(desc.Winding) > 1 || static_cast<unsigned>(desc.Blend) > 2) {
        Fail("Invalid pipeline shaders, format, state or device ownership."); return nullptr;
    }
    if (!SDL_GPUTextureSupportsFormat(_Device, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
        Fail("Unsupported pipeline color format."); return nullptr;
    }
    SDL_GPUColorTargetDescription color{};
    color.format = format;
    auto& blend = color.blend_state;
    blend.enable_blend = desc.Blend != BlendMode::Opaque;
    blend.src_color_blendfactor = desc.Blend == BlendMode::Alpha ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex->Native; info.fragment_shader = fragment->Native;
    const SDL_GPUPrimitiveType primitives[] = {SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP, SDL_GPU_PRIMITIVETYPE_LINELIST, SDL_GPU_PRIMITIVETYPE_LINESTRIP, SDL_GPU_PRIMITIVETYPE_POINTLIST};
    info.primitive_type = primitives[static_cast<unsigned>(desc.Primitive)];
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    const SDL_GPUCullMode culls[] = {SDL_GPU_CULLMODE_NONE, SDL_GPU_CULLMODE_FRONT, SDL_GPU_CULLMODE_BACK};
    info.rasterizer_state.cull_mode = culls[static_cast<unsigned>(desc.Cull)];
    info.rasterizer_state.front_face = desc.Winding == FrontFace::CounterClockwise ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
    auto* native = SDL_CreateGPUGraphicsPipeline(_Device, &info);
    if (!native) { FailSDL(); return nullptr; }
    try { return new PipelineSDL(this, NextID(), native, desc.ColorFormat, fragment->SamplerCount); }
    catch (...) { SDL_ReleaseGPUGraphicsPipeline(_Device, native); throw; }
}
}
