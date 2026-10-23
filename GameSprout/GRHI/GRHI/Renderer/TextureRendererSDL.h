#pragma once
#include "GRHIApi.h"

namespace GRHI {
    class DeviceSDL;
    // One per surface. Keeps image-drawing resources, never retains the caller's texture.
    class TextureRendererSDL final {
    public:
        explicit TextureRendererSDL(DeviceSDL* device) : _Device(device) {}
        ~TextureRendererSDL();
        TextureRendererSDL(const TextureRendererSDL&) = delete;
        TextureRendererSDL& operator=(const TextureRendererSDL&) = delete;
        bool Draw(ISurface& surface, ITexture* texture, const Color& clearColor);
    private:
        bool Initialize();
        bool EnsurePipeline(TextureFormat format);
        DeviceSDL* const _Device;
        ISampler* _Sampler = nullptr;
        IShader* _VertexShader = nullptr;
        IShader* _FragmentShader = nullptr;
        IGraphicsPipeline* _Pipeline = nullptr;
    };
}
