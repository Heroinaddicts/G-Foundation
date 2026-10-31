#ifndef __SurfaceSDL_h__
#define __SurfaceSDL_h__
#include "DeviceSDL.h"
namespace GRHI {
class TextureRendererSDL;
class SurfaceSDL final : public ISurface {
public:
    ~SurfaceSDL() override { Release(); }
    SurfaceSDL(const SurfaceSDL&) = delete;
    SurfaceSDL& operator=(const SurfaceSDL&) = delete;
    static SurfaceSDL* CreateSurfaceSDL(DeviceSDL*, GRHIWindowHandle);
    void Release() override;
    TextureFormat GetColorFormat() const override;
    bool Draw(ITexture*, const Color&) override;
    AcquireFrameResult AcquireFrame(ICommandBuffer*, SurfaceFrame&) override;
private:
    SurfaceSDL(DeviceSDL* device, SDL_Window* window,GRHIWindowHandle handle) : _Device(device), _Window(window),_HostHandle(handle) {}
    DeviceSDL* const _Device;
    SDL_Window* _Window;
    const GRHIWindowHandle _HostHandle;
    TextureRendererSDL* _Renderer = nullptr;
};
}
#endif
