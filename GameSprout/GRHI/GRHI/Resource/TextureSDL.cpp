#include "TextureSDL.h"
#include "DeviceSDL.h"
#include <cstring>

namespace GRHI {
TextureSDL::TextureSDL(DeviceSDL* owner, uint64_t id, SDL_GPUTexture* native, const TextureDesc& desc)
    : ITexture(id), Owner(owner), Native(native), Desc(desc) {
    Owner->RegisterResource(static_cast<ITexture*>(this),this,id,ResourceKindSDL::Texture);
}

TextureSDL::~TextureSDL() {
    Release();
}

SDL_GPUTextureFormat ToSDL(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8_UNorm: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGBA8_UNorm_sRGB: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::BGRA8_UNorm: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case TextureFormat::BGRA8_UNorm_sRGB: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    case TextureFormat::D32_Float: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}
TextureFormat FromSDL(SDL_GPUTextureFormat format) {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8_UNorm;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return TextureFormat::RGBA8_UNorm_sRGB;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8_UNorm;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return TextureFormat::BGRA8_UNorm_sRGB;
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT: return TextureFormat::D32_Float;
    default: return TextureFormat::Unknown;
    }
}

void TextureSDL::Release() {
    Owner->RemoveResource(static_cast<ITexture*>(this));
    if (Token) { Owner->RemoveTarget(Token); Token = 0; }
    if (Native) { SDL_ReleaseGPUTexture(Owner->Native(), Native); Native = nullptr; }
}

uint32_t TextureSDL::GetWidth() const { return Desc.Width; }
uint32_t TextureSDL::GetHeight() const { return Desc.Height; }
TextureFormat TextureSDL::GetFormat() const { return Desc.Format; }
ColorTargetView TextureSDL::GetColorTargetView() const { return {(Desc.Usage&TextureUsage::ColorTarget)!=TextureUsage::None ? Token : 0}; }
DepthTargetView TextureSDL::GetDepthTargetView() const { return {(Desc.Usage&TextureUsage::DepthStencilTarget)!=TextureUsage::None ? Token : 0}; }

ITexture* DeviceSDL::CreateTexture(const TextureDesc& desc, const TextureInitialData* initial) {
    ClearError();
    const auto format = ToSDL(desc.Format);
    const auto usage = static_cast<unsigned>(desc.Usage);
    const bool depth=desc.Format==TextureFormat::D32_Float;
    if (!_Device || !desc.Width || !desc.Height || format == SDL_GPU_TEXTUREFORMAT_INVALID || !usage || (usage & ~7u) ||
        (depth ? (usage!=4u && usage!=5u) || initial!=nullptr : (usage&4u)!=0)) {
        Fail("Invalid texture description or released device."); return nullptr;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.width = desc.Width; info.height = desc.Height;
    info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if ((desc.Usage & TextureUsage::Sampled) != TextureUsage::None) info.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if ((desc.Usage & TextureUsage::ColorTarget) != TextureUsage::None) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if ((desc.Usage & TextureUsage::DepthStencilTarget) != TextureUsage::None) info.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (!SDL_GPUTextureSupportsFormat(_Device, format, info.type, info.usage)) {
        Fail("Texture format/usage combination is unsupported."); return nullptr;
    }
    SDL_GPUTexture* native = SDL_CreateGPUTexture(_Device, &info);
    if (!native) { FailSDL(); return nullptr; }
    TextureSDL* texture = nullptr;
    try {
        texture = new TextureSDL(this, NextID(), native, desc);
        native = nullptr;
        if ((desc.Usage & (TextureUsage::ColorTarget|TextureUsage::DepthStencilTarget)) != TextureUsage::None)
            texture->Token = AddTarget({texture->Native, desc.Format, desc.Width, desc.Height, nullptr});
        if(initial&&!UploadTextureRows(texture,0,desc.Height,*initial)) {
            delete texture;
            return nullptr;
        }
        return texture;
    } catch (...) {
        if (native) SDL_ReleaseGPUTexture(_Device, native);
        delete texture;
        throw;
    }
}

bool DeviceSDL::UploadTextureRows(ITexture* target, uint32_t firstRow, uint32_t rowCount,
                                  const TextureInitialData& initial) {
    ClearError();
    auto* texture=Resource<TextureSDL>(target,ResourceKindSDL::Texture);
    if(!_Device||!OnMainThread()||!texture||!texture->Native||!rowCount||
       texture->Desc.Format==TextureFormat::D32_Float||firstRow>texture->Desc.Height||
       rowCount>texture->Desc.Height-firstRow) return Fail("Invalid texture row upload target or range.");
    const uint64_t rowBytes=uint64_t(texture->Desc.Width)*4;
    if(rowBytes>UINT32_MAX||rowCount>UINT32_MAX/rowBytes)
        return Fail("Texture upload exceeds SDL transfer-buffer size limits.");
    const auto pitch=initial.RowPitch?initial.RowPitch:static_cast<size_t>(rowBytes);
    if(!initial.Pixels||rowBytes>initial.DataSize||pitch<rowBytes||
       (rowCount>1&&pitch>(initial.DataSize-rowBytes)/(rowCount-1)))
        return Fail("Invalid initial texture data, row pitch or upload size.");
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size=static_cast<uint32_t>(rowBytes*rowCount);
    auto* transfer=SDL_CreateGPUTransferBuffer(_Device,&info);
    if(!transfer)return FailSDL();
    const auto release=[&] { SDL_ReleaseGPUTransferBuffer(_Device,transfer); };
    auto* mapped=static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(_Device,transfer,false));
    if(!mapped){release();return FailSDL();}
    if(pitch==rowBytes)std::memcpy(mapped,initial.Pixels,info.size);
    else for(uint32_t row=0;row<rowCount;++row)
        std::memcpy(mapped+row*rowBytes,static_cast<const uint8_t*>(initial.Pixels)+row*pitch,rowBytes);
    SDL_UnmapGPUTransferBuffer(_Device,transfer);
    auto* upload=SDL_AcquireGPUCommandBuffer(_Device);
    if(!upload){release();return FailSDL();}
    auto* copy=SDL_BeginGPUCopyPass(upload);
    if(!copy) {
        SDL_CancelGPUCommandBuffer(upload);release();return FailSDL();
    }
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer=transfer;
    SDL_GPUTextureRegion destination{};
    destination.texture=texture->Native;
    destination.y=firstRow;
    destination.w=texture->Desc.Width;destination.h=rowCount;destination.d=1;
    SDL_UploadToGPUTexture(copy,&source,&destination,false);
    SDL_EndGPUCopyPass(copy);
    const bool submitted=SDL_SubmitGPUCommandBuffer(upload);
    release();
    return submitted||FailSDL();
}
}
