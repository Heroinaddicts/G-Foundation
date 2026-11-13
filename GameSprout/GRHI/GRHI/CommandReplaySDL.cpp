#include "CommandBufferSDL.h"
#include "Resource/BufferSDL.h"
#include <cstring>

namespace GRHI {
namespace {
template<typename... Handlers>
struct VisitOperation : Handlers... {using Handlers::operator()...;};
}

void RenderPassSDL::Encode(SDL_GPURenderPass* pass) const {
    const auto replay=VisitOperation{
        [&](const BindPipelineSDL& operation){SDL_BindGPUGraphicsPipeline(pass,operation.Pipeline);},
        [&](const BindViewportSDL& operation){SDL_SetGPUViewport(pass,&operation.View);},
        [&](const BindVertexSDL& operation){SDL_BindGPUVertexBuffers(pass,operation.Slot,&operation.Binding,1);},
        [&](const BindIndexSDL& operation){SDL_BindGPUIndexBuffer(pass,&operation.Binding,operation.Format);},
        [&](const BindVertexStorageSDL& operation){SDL_BindGPUVertexStorageBuffers(pass,operation.Slot,&operation.Buffer,1);},
        [&](const BindUniformSDL& operation){
            const auto& bytes=*operation.Bytes;
            auto push=operation.Vertex?SDL_PushGPUVertexUniformData:SDL_PushGPUFragmentUniformData;
            push(Command->Native,operation.Slot,bytes.data(),static_cast<uint32_t>(bytes.size()));
        },
        [&](const BindSamplersSDL& operation){
            SDL_BindGPUFragmentSamplers(pass,0,_NativeSamplers.data()+operation.First,operation.Count);
        },
        [&](const NativeDrawSDL& operation){
            if(operation.Indexed)
                SDL_DrawGPUIndexedPrimitives(pass,operation.Count,operation.Instances,operation.First,
                    operation.VertexOffset,operation.FirstInstance);
            else
                SDL_DrawGPUPrimitives(pass,operation.Count,operation.Instances,operation.First,operation.FirstInstance);
        },
    };
    for(const auto& operation:_Operations)std::visit(replay,operation);
}
bool CommandBufferSDL::Encode() {
    SDL_GPUTransferBuffer* transfer=nullptr;
    if(!_Uploads.empty()) {
        transfer=Owner->UploadTransfer(static_cast<uint32_t>(_UploadBytes.size()));
        if(!transfer)return Fail(SDL_GetError());
        void* mapped=SDL_MapGPUTransferBuffer(Owner->Native(),transfer,true);
        if(!mapped)return Fail(SDL_GetError());
        std::memcpy(mapped,_UploadBytes.data(),_UploadBytes.size());
        SDL_UnmapGPUTransferBuffer(Owner->Native(),transfer);
    }

    auto nextUpload=_Uploads.begin();
    const auto encodeUploads=[&](size_t beforePass) {
        if(nextUpload==_Uploads.end() || nextUpload->BeforePass!=beforePass)return true;
        auto* copy=SDL_BeginGPUCopyPass(Native);
        if(!copy)return Fail(SDL_GetError());
        do {
            const auto& upload=*nextUpload;
            auto* buffer=Owner->Resource<BufferSDL>(upload.Destination.Pointer,ResourceKindSDL::Buffer);
            const SDL_GPUTransferBufferLocation source{transfer,upload.SourceOffset};
            const SDL_GPUBufferRegion destination{buffer->Native,upload.DestinationOffset,upload.Size};
            SDL_UploadToGPUBuffer(copy,&source,&destination,upload.Cycle);
            ++nextUpload;
        } while(nextUpload!=_Uploads.end() && nextUpload->BeforePass==beforePass);
        SDL_EndGPUCopyPass(copy);
        return true;
    };

    const SDL_GPULoadOp loads[]={SDL_GPU_LOADOP_CLEAR,SDL_GPU_LOADOP_LOAD,SDL_GPU_LOADOP_DONT_CARE};
    for(size_t passIndex=0;passIndex<Passes.size();++passIndex) {
        if(!encodeUploads(passIndex))return false;
        const auto* pass=Passes[passIndex];
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
        pass->Encode(nativePass);
        SDL_EndGPURenderPass(nativePass);
    }
    return encodeUploads(Passes.size());
}
}
