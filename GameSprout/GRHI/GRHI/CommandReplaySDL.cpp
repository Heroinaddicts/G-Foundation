#include "CommandBufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include "ReplayStateSDL.h"

namespace GRHI {
bool CommandBufferSDL::Encode() {
    const SDL_GPULoadOp loads[]={SDL_GPU_LOADOP_CLEAR,SDL_GPU_LOADOP_LOAD,SDL_GPU_LOADOP_DONT_CARE};
    for(const auto* pass:Passes) {
        SDL_GPUColorTargetInfo color{};
        color.texture=pass->Target.Texture;color.load_op=loads[static_cast<unsigned>(pass->Desc.Load)];
        color.store_op=pass->Desc.Store==StoreOp::Store?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
        const auto& clear=pass->Desc.ClearColor;color.clear_color={clear.R,clear.G,clear.B,clear.A};
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture=pass->Depth.Texture;depth.clear_depth=pass->Desc.ClearDepth;
        depth.load_op=loads[static_cast<unsigned>(pass->Desc.DepthLoad)];
        depth.store_op=pass->Desc.DepthStore==StoreOp::Store?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* nativePass=SDL_BeginGPURenderPass(Native,color.texture?&color:nullptr,color.texture?1:0,depth.texture?&depth:nullptr);
        if(!nativePass)return Fail(SDL_GetError());
        ReplayStateSDL state;
        for(const auto& draw:pass->Draws) {
            auto* pipeline=Owner->Resource<PipelineSDL>(draw.Pipeline.Pointer,ResourceKindSDL::Pipeline);
            if(state.Pipeline(draw.Pipeline))SDL_BindGPUGraphicsPipeline(nativePass,pipeline->Native);
            const auto& v=draw.View;
            const SDL_GPUViewport viewport{v.X,v.Y,v.Width,v.Height,v.MinDepth,v.MaxDepth};
            if(state.View(v))SDL_SetGPUViewport(nativePass,&viewport);
            for(const auto& binding:pipeline->Bindings) {
                const auto& source=draw.Vertices[binding.Binding];
                if(!state.Vertex(binding.Binding,source))continue;
                auto* buffer=Owner->Resource<BufferSDL>(source.Resource.Pointer,ResourceKindSDL::Buffer);
                const SDL_GPUBufferBinding nativeBinding{buffer->Native,static_cast<uint32_t>(source.Offset)};
                SDL_BindGPUVertexBuffers(nativePass,binding.Binding,&nativeBinding,1);
            }
            for(uint32_t slot=0;slot<pipeline->VertexUniformCount;++slot) {
                if(!state.Uniform(true,slot,draw.VertexUniforms[slot]))continue;
                const auto& bytes=*draw.VertexUniforms[slot];
                SDL_PushGPUVertexUniformData(Native,slot,bytes.data(),static_cast<uint32_t>(bytes.size()));
            }
            for(uint32_t slot=0;slot<pipeline->FragmentUniformCount;++slot) {
                if(!state.Uniform(false,slot,draw.FragmentUniforms[slot]))continue;
                const auto& bytes=*draw.FragmentUniforms[slot];
                SDL_PushGPUFragmentUniformData(Native,slot,bytes.data(),static_cast<uint32_t>(bytes.size()));
            }
            if(pipeline->SamplerCount && state.Samplers(draw,pipeline->SamplerCount)) {
                SDL_GPUTextureSamplerBinding samplers[16]{};
                for(uint32_t slot=0;slot<pipeline->SamplerCount;++slot) {
                    auto* texture=Owner->Resource<TextureSDL>(draw.Textures[slot].Pointer,ResourceKindSDL::Texture);
                    auto* sampler=Owner->Resource<SamplerSDL>(draw.Samplers[slot].Pointer,ResourceKindSDL::Sampler);
                    samplers[slot]={texture->Native,sampler->Native};
                }
                SDL_BindGPUFragmentSamplers(nativePass,0,samplers,pipeline->SamplerCount);
            }
            if(draw.Indexed) {
                if(state.Index(draw.Index,draw.Indices)) {
                    auto* buffer=Owner->Resource<BufferSDL>(draw.Index.Resource.Pointer,ResourceKindSDL::Buffer);
                    const SDL_GPUBufferBinding binding{buffer->Native,static_cast<uint32_t>(draw.Index.Offset)};
                    SDL_BindGPUIndexBuffer(nativePass,&binding,draw.Indices==IndexFormat::UInt16?SDL_GPU_INDEXELEMENTSIZE_16BIT:SDL_GPU_INDEXELEMENTSIZE_32BIT);
                }
                SDL_DrawGPUIndexedPrimitives(nativePass,draw.Count,draw.Instances,draw.First,draw.VertexOffset,draw.FirstInstance);
            } else SDL_DrawGPUPrimitives(nativePass,draw.Count,draw.Instances,draw.First,draw.FirstInstance);
        }
        SDL_EndGPURenderPass(nativePass);
    }
    return true;
}
}
