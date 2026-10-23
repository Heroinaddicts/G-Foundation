#include "TextureSDL.h"
#include "DeviceSDL.h"
#include <cstring>

namespace GRHI {
TextureSDL::TextureSDL(DeviceSDL* owner, uint64_t id, SDL_GPUTexture* native, const TextureDesc& desc)
    : ITexture(id), Owner(owner), Native(native), Desc(desc) {}

TextureSDL::~TextureSDL() {
    Release();
}

SDL_GPUTextureFormat ToSDL(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8_UNorm: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGBA8_UNorm_sRGB: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::BGRA8_UNorm: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case TextureFormat::BGRA8_UNorm_sRGB: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}
TextureFormat FromSDL(SDL_GPUTextureFormat format) {
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8_UNorm;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return TextureFormat::RGBA8_UNorm_sRGB;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8_UNorm;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return TextureFormat::BGRA8_UNorm_sRGB;
    default: return TextureFormat::Unknown;
    }
}

void TextureSDL::Release() {
    if (Token) { Owner->RemoveTarget(Token); Token = 0; }
    if (Native) { SDL_ReleaseGPUTexture(Owner->Native(), Native); Native = nullptr; }
}

uint32_t TextureSDL::GetWidth() const { return Desc.Width; }
uint32_t TextureSDL::GetHeight() const { return Desc.Height; }
TextureFormat TextureSDL::GetFormat() const { return Desc.Format; }
ColorTargetView TextureSDL::GetColorTargetView() const { return {Token}; }

ITexture* DeviceSDL::CreateTexture(const TextureDesc& desc, const TextureInitialData* initial) {
    ClearError();
    const auto format = ToSDL(desc.Format);
    const auto usage = static_cast<unsigned>(desc.Usage);
    if (!_Device || !desc.Width || !desc.Height || format == SDL_GPU_TEXTUREFORMAT_INVALID || !usage || (usage & ~3u)) {
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
    if (!SDL_GPUTextureSupportsFormat(_Device, format, info.type, info.usage)) {
        Fail("Texture format/usage combination is unsupported."); return nullptr;
    }
    const uint64_t rowBytes = uint64_t(desc.Width) * 4;
    if (initial && (rowBytes > UINT32_MAX || desc.Height > UINT32_MAX / rowBytes)) {
        Fail("Texture upload exceeds SDL transfer-buffer size limits."); return nullptr;
    }
    const uint64_t totalBytes = initial ? rowBytes * desc.Height : 0;
    size_t pitch = 0;
    if (initial) {
        pitch = initial->RowPitch ? initial->RowPitch : static_cast<size_t>(rowBytes);
        // Validate the input span without overflowing a size_t multiplication.
        if (!initial->Pixels || totalBytes > UINT32_MAX || rowBytes > initial->DataSize || pitch < rowBytes ||
            (desc.Height > 1 && pitch > (initial->DataSize - rowBytes) / (desc.Height - 1))) {
            Fail("Invalid initial texture data, row pitch or upload size."); return nullptr;
        }
    }
    SDL_GPUTexture* native = SDL_CreateGPUTexture(_Device, &info);
    if (!native) { FailSDL(); return nullptr; }
    TextureSDL* texture = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    SDL_GPUCommandBuffer* upload = nullptr;
    try {
        texture = new TextureSDL(this, NextID(), native, desc);
        native = nullptr;
        if ((desc.Usage & TextureUsage::ColorTarget) != TextureUsage::None)
            texture->Token = AddTarget({texture->Native, desc.Format, desc.Width, desc.Height, nullptr});
        if (initial) {
            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = static_cast<uint32_t>(totalBytes);
            transfer = SDL_CreateGPUTransferBuffer(_Device, &transferInfo);
            if (!transfer) { FailSDL(); delete texture; return nullptr; }
            void* mapped = SDL_MapGPUTransferBuffer(_Device, transfer, false);
            if (!mapped) { FailSDL(); SDL_ReleaseGPUTransferBuffer(_Device, transfer); delete texture; return nullptr; }
            for (uint32_t y = 0; y < desc.Height; ++y)
                std::memcpy(static_cast<uint8_t*>(mapped) + y * rowBytes,
                    static_cast<const uint8_t*>(initial->Pixels) + y * pitch, static_cast<size_t>(rowBytes));
            SDL_UnmapGPUTransferBuffer(_Device, transfer);
            upload = SDL_AcquireGPUCommandBuffer(_Device);
            if (!upload) { FailSDL(); SDL_ReleaseGPUTransferBuffer(_Device, transfer); delete texture; return nullptr; }
            auto* copy = SDL_BeginGPUCopyPass(upload);
            if (!copy) {
                FailSDL(); SDL_CancelGPUCommandBuffer(upload); SDL_ReleaseGPUTransferBuffer(_Device, transfer); delete texture; return nullptr;
            }
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            SDL_GPUTextureRegion destination{};
            destination.texture = texture->Native;
            destination.w = desc.Width; destination.h = desc.Height; destination.d = 1;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);
            const bool submitted = SDL_SubmitGPUCommandBuffer(upload);
            upload = nullptr;
            if (!submitted) FailSDL();
            SDL_ReleaseGPUTransferBuffer(_Device, transfer); transfer = nullptr;
            if (!submitted) { delete texture; return nullptr; }
        }
        return texture;
    } catch (...) {
        if (upload) SDL_CancelGPUCommandBuffer(upload);
        if (transfer) SDL_ReleaseGPUTransferBuffer(_Device, transfer);
        if (native) SDL_ReleaseGPUTexture(_Device, native);
        delete texture;
        throw;
    }
}
}
