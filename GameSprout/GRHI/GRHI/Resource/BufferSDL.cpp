#include "BufferSDL.h"
#include "DeviceSDL.h"
#include <cstring>
#include <utility>

namespace GRHI {
BufferSDL::BufferSDL(DeviceSDL* owner,uint64_t id,SDL_GPUBuffer* native,const BufferDesc& desc,std::vector<uint8_t> indices)
    : IBuffer(id),Owner(owner),Native(native),Desc(desc),IndexBytes(std::move(indices)) {
    Owner->RegisterResource(static_cast<IBuffer*>(this),this,id,ResourceKindSDL::Buffer);
}
BufferSDL::~BufferSDL() { Release(); }
void BufferSDL::Release() {
    Owner->RemoveResource(static_cast<IBuffer*>(this));
    if(Native) { SDL_ReleaseGPUBuffer(Owner->Native(),Native);Native=nullptr; }
}
IBuffer* DeviceSDL::CreateBuffer(const BufferDesc& desc,const BufferInitialData* initial) {
    ClearError();
    const unsigned usage=static_cast<unsigned>(desc.Usage);
    if(!_Device || !desc.Size || desc.Size>UINT32_MAX || !usage || (usage&~3u) ||
       (initial && (initial->DataSize>desc.Size || (initial->DataSize && !initial->Data)))) {
        Fail("Invalid buffer size, usage, initial span or released device.");return nullptr;
    }
    std::vector<uint8_t> bytes(desc.Size,0);
    if(initial && initial->DataSize)std::memcpy(bytes.data(),initial->Data,initial->DataSize);
    SDL_GPUBufferCreateInfo info{};info.size=static_cast<uint32_t>(desc.Size);
    if((desc.Usage&BufferUsage::Vertex)!=BufferUsage::None)info.usage|=SDL_GPU_BUFFERUSAGE_VERTEX;
    if((desc.Usage&BufferUsage::Index)!=BufferUsage::None)info.usage|=SDL_GPU_BUFFERUSAGE_INDEX;
    auto* native=SDL_CreateGPUBuffer(_Device,&info);
    if(!native){FailSDL();return nullptr;}
    SDL_GPUTransferBuffer* transfer=nullptr;
    SDL_GPUCommandBuffer* upload=nullptr;
    BufferSDL* buffer=nullptr;
    try {
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;transferInfo.size=info.size;
        transfer=SDL_CreateGPUTransferBuffer(_Device,&transferInfo);
        if(!transfer){FailSDL();SDL_ReleaseGPUBuffer(_Device,native);return nullptr;}
        void* mapped=SDL_MapGPUTransferBuffer(_Device,transfer,false);
        if(!mapped){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);SDL_ReleaseGPUBuffer(_Device,native);return nullptr;}
        std::memcpy(mapped,bytes.data(),bytes.size());SDL_UnmapGPUTransferBuffer(_Device,transfer);
        upload=SDL_AcquireGPUCommandBuffer(_Device);
        if(!upload){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);SDL_ReleaseGPUBuffer(_Device,native);return nullptr;}
        auto* copy=SDL_BeginGPUCopyPass(upload);
        if(!copy){FailSDL();SDL_CancelGPUCommandBuffer(upload);SDL_ReleaseGPUTransferBuffer(_Device,transfer);SDL_ReleaseGPUBuffer(_Device,native);return nullptr;}
        SDL_GPUTransferBufferLocation source{};source.transfer_buffer=transfer;
        SDL_GPUBufferRegion target{};target.buffer=native;target.size=info.size;
        SDL_UploadToGPUBuffer(copy,&source,&target,false);SDL_EndGPUCopyPass(copy);
        const bool success=SDL_SubmitGPUCommandBuffer(upload);upload=nullptr;
        SDL_ReleaseGPUTransferBuffer(_Device,transfer);transfer=nullptr;
        if(!success){FailSDL();SDL_ReleaseGPUBuffer(_Device,native);return nullptr;}
        if((desc.Usage&BufferUsage::Index)==BufferUsage::None)bytes.clear();
        buffer=new BufferSDL(this,NextID(),native,desc,std::move(bytes));native=nullptr;
        return buffer;
    } catch(...) {
        if(upload)SDL_CancelGPUCommandBuffer(upload);
        if(transfer)SDL_ReleaseGPUTransferBuffer(_Device,transfer);
        if(native)SDL_ReleaseGPUBuffer(_Device,native);
        delete buffer;throw;
    }
}
}
