#include "TextureSDL.h"
#include "DeviceSDL.h"
#include <cstring>
#include <algorithm>

namespace GRHI {
bool DeviceSDL::ReadTexture(ITexture* value,std::vector<uint8_t>& pixels) {
    pixels.clear();ClearError();
    auto* texture=Resource<TextureSDL>(value,ResourceKindSDL::Texture);
    if(!_Device || !OnMainThread() || !_Commands.empty() || !texture || !texture->Native || texture->Desc.Format==TextureFormat::D32_Float) {
        return Fail("ReadTexture requires a live local color texture, the device thread and no outstanding commands.");
    }
    const uint64_t size=uint64_t(texture->Desc.Width)*texture->Desc.Height*4;
    if(!size || size>UINT32_MAX)return Fail("ReadTexture exceeds transfer-buffer limits.");
    SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=static_cast<uint32_t>(size);
    auto* transfer=SDL_CreateGPUTransferBuffer(_Device,&info);
    if(!transfer)return FailSDL();
    auto* command=SDL_AcquireGPUCommandBuffer(_Device);
    if(!command){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);return false;}
    auto* copy=SDL_BeginGPUCopyPass(command);
    if(!copy){FailSDL();SDL_CancelGPUCommandBuffer(command);SDL_ReleaseGPUTransferBuffer(_Device,transfer);return false;}
    SDL_GPUTextureRegion source{};source.texture=texture->Native;source.w=texture->Desc.Width;source.h=texture->Desc.Height;source.d=1;
    SDL_GPUTextureTransferInfo destination{};destination.transfer_buffer=transfer;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if(!fence){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);return false;}
    const bool waited=SDL_WaitForGPUFences(_Device,true,&fence,1);SDL_ReleaseGPUFence(_Device,fence);
    if(!waited){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);return false;}
    void* mapped=SDL_MapGPUTransferBuffer(_Device,transfer,false);
    if(!mapped){FailSDL();SDL_ReleaseGPUTransferBuffer(_Device,transfer);return false;}
    try {pixels.resize(static_cast<size_t>(size));std::memcpy(pixels.data(),mapped,pixels.size());}
    catch(...) {SDL_UnmapGPUTransferBuffer(_Device,transfer);SDL_ReleaseGPUTransferBuffer(_Device,transfer);throw;}
    SDL_UnmapGPUTransferBuffer(_Device,transfer);SDL_ReleaseGPUTransferBuffer(_Device,transfer);
    if(texture->Desc.Format==TextureFormat::BGRA8_UNorm || texture->Desc.Format==TextureFormat::BGRA8_UNorm_sRGB)
        for(size_t offset=0;offset<pixels.size();offset+=4)std::swap(pixels[offset],pixels[offset+2]);
    ClearError();return true;
}
}
