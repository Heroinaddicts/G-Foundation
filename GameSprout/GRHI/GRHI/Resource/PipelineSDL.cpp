#include "PipelineSDL.h"
#include "DeviceSDL.h"
#include "ShaderSDL.h"
#include <set>
#include <algorithm>

namespace GRHI {
PipelineSDL::PipelineSDL(DeviceSDL* owner, uint64_t id, SDL_GPUGraphicsPipeline* native,const GraphicsPipelineDesc& desc,
                         uint32_t samplers,uint32_t vertexUniforms,uint32_t fragmentUniforms)
    : IGraphicsPipeline(id),Owner(owner),Native(native),Format(desc.ColorFormat),SamplerCount(samplers),
      VertexUniformCount(vertexUniforms),FragmentUniformCount(fragmentUniforms),DepthFormat(desc.DepthFormat) {
    for(size_t index=0;index<desc.VertexBindingCount;++index)Bindings.push_back(desc.VertexBindings[index]);
    for(size_t index=0;index<desc.VertexAttributeCount;++index) {
        const auto& a=desc.VertexAttributes[index];
        const uint32_t size=a.Format==VertexFormat::Float2?8:a.Format==VertexFormat::Float3?12:16;
        AttributeEnds[a.Binding]=std::max(AttributeEnds[a.Binding],a.Offset+size);
    }
    Owner->RegisterResource(static_cast<IGraphicsPipeline*>(this),this,id,ResourceKindSDL::Pipeline);
}

PipelineSDL::~PipelineSDL() {
    Release();
}

void PipelineSDL::Release() {
    Owner->RemoveResource(static_cast<IGraphicsPipeline*>(this));
    if (Native) {
        SDL_ReleaseGPUGraphicsPipeline(Owner->Native(), Native);
        Native = nullptr;
    }
}

TextureFormat PipelineSDL::GetColorFormat() const { return Format; }

IGraphicsPipeline* DeviceSDL::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    ClearError();
    auto* vertex = Resource<ShaderSDL>(desc.VertexShader,ResourceKindSDL::Shader);
    auto* fragment = Resource<ShaderSDL>(desc.FragmentShader,ResourceKindSDL::Shader);
    const auto format = ToSDL(desc.ColorFormat);
    if (!_Device || !vertex || !fragment || vertex->Owner != this || fragment->Owner != this ||
        !vertex->Native || !fragment->Native || vertex->Stage != ShaderStage::Vertex || fragment->Stage != ShaderStage::Fragment ||
        format == SDL_GPU_TEXTUREFORMAT_INVALID || desc.ColorFormat==TextureFormat::D32_Float || static_cast<unsigned>(desc.Primitive) > 4 ||
        static_cast<unsigned>(desc.Cull) > 2 || static_cast<unsigned>(desc.Winding) > 1 || static_cast<unsigned>(desc.Blend) > 2 ||
        static_cast<unsigned>(desc.DepthCompare)>7 || (desc.DepthFormat!=TextureFormat::Unknown && desc.DepthFormat!=TextureFormat::D32_Float) ||
        ((desc.DepthTest || desc.DepthWrite) && desc.DepthFormat==TextureFormat::Unknown) ||
        desc.VertexBindingCount>16 || desc.VertexAttributeCount>16 || (desc.VertexBindingCount && !desc.VertexBindings) ||
        (desc.VertexAttributeCount && !desc.VertexAttributes)) {
        Fail("Invalid pipeline shaders, format, state or device ownership."); return nullptr;
    }
    std::vector<SDL_GPUVertexBufferDescription> buffers;
    std::vector<SDL_GPUVertexAttribute> attributes;
    std::array<uint32_t,16> strides{};
    std::array<bool,16> used{};
    std::set<uint32_t> locations;
    for(size_t index=0;index<desc.VertexBindingCount;++index) {
        const auto& binding=desc.VertexBindings[index];
        if(binding.Binding>=16 || !binding.Stride || binding.Stride%4 || binding.Stride>2048 || strides[binding.Binding]) {
            Fail("Invalid, duplicate or oversized vertex binding.");return nullptr;
        }
        strides[binding.Binding]=binding.Stride;
        buffers.push_back({binding.Binding,binding.Stride,SDL_GPU_VERTEXINPUTRATE_VERTEX,0});
    }
    const SDL_GPUVertexElementFormat formats[]={SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        SDL_GPU_VERTEXELEMENTFORMAT_UINT4};
    for(size_t index=0;index<desc.VertexAttributeCount;++index) {
        const auto& a=desc.VertexAttributes[index];
        const auto f=static_cast<unsigned>(a.Format);
        const uint32_t size=f==0?8:f==1?12:16;
        if(a.Location>=16 || a.Binding>=16 || f>3 || !strides[a.Binding] || a.Offset>strides[a.Binding] ||
           size>strides[a.Binding]-a.Offset || a.Offset%4 || !locations.insert(a.Location).second) {
            Fail("Invalid vertex attribute location, format, offset or binding.");return nullptr;
        }
        used[a.Binding]=true;attributes.push_back({a.Location,a.Binding,formats[f],a.Offset});
    }
    for(const auto& binding:buffers)if(!used[binding.slot]) {Fail("Each vertex binding must have at least one attribute.");return nullptr;}
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
    info.rasterizer_state.enable_depth_clip=true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
    info.vertex_input_state.vertex_buffer_descriptions=buffers.data();info.vertex_input_state.num_vertex_buffers=static_cast<uint32_t>(buffers.size());
    info.vertex_input_state.vertex_attributes=attributes.data();info.vertex_input_state.num_vertex_attributes=static_cast<uint32_t>(attributes.size());
    if(desc.DepthFormat!=TextureFormat::Unknown) {
        if(!SDL_GPUTextureSupportsFormat(_Device,ToSDL(desc.DepthFormat),SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {Fail("Unsupported depth format.");return nullptr;}
        info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=ToSDL(desc.DepthFormat);
    }
    const SDL_GPUCompareOp comparisons[]={SDL_GPU_COMPAREOP_NEVER,SDL_GPU_COMPAREOP_LESS,SDL_GPU_COMPAREOP_EQUAL,SDL_GPU_COMPAREOP_LESS_OR_EQUAL,SDL_GPU_COMPAREOP_GREATER,SDL_GPU_COMPAREOP_NOT_EQUAL,SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,SDL_GPU_COMPAREOP_ALWAYS};
    info.depth_stencil_state.enable_depth_test=desc.DepthTest;info.depth_stencil_state.enable_depth_write=desc.DepthWrite;
    info.depth_stencil_state.compare_op=comparisons[static_cast<unsigned>(desc.DepthCompare)];
    auto* native = SDL_CreateGPUGraphicsPipeline(_Device, &info);
    if (!native) { FailSDL(); return nullptr; }
    try { return new PipelineSDL(this,NextID(),native,desc,fragment->SamplerCount,vertex->UniformCount,fragment->UniformCount); }
    catch (...) { SDL_ReleaseGPUGraphicsPipeline(_Device, native); throw; }
}
}
