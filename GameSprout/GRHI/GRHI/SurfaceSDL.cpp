#include "SurfaceSDL.h"
#include "CommandBufferSDL.h"
#include "Renderer/TextureRendererSDL.h"
#if defined(SDL_PLATFORM_MACOS)
#include "SurfaceCocoa.h"
#endif

namespace GRHI {
SurfaceSDL* SurfaceSDL::CreateSurfaceSDL(DeviceSDL* device, GRHIWindowHandle handle) {
    device->ClearError();
    if (!device->OnMainThread()) { device->Fail("Surface creation requires the device/window main thread."); return nullptr; }
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) { device->FailSDL(); return nullptr; }
    if (!SDL_SetBooleanProperty(props,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,true)) {
        device->FailSDL();SDL_DestroyProperties(props);return nullptr;
    }
    bool configured = false;
#if defined(SDL_PLATFORM_MACOS)
    configured = SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, handle);
#elif defined(SDL_PLATFORM_WINDOWS)
    configured = SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, handle);
#else
    configured = SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER, static_cast<Sint64>(handle));
#endif
    SDL_Window* window = configured ? SDL_CreateWindowWithProperties(props) : nullptr;
    SDL_DestroyProperties(props);
    if (!window) { device->FailSDL(); return nullptr; }
    if (!SDL_ClaimWindowForGPUDevice(device->Native(), window)) {
        device->FailSDL(); SDL_DestroyWindow(window); return nullptr;
    }
    // Shaders output linear colors. Use an sRGB swapchain so presentation encodes
    // RGB once, matching sRGB texture sampling and linear alpha blending.
    if (!SDL_WindowSupportsGPUSwapchainComposition(device->Native(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR) ||
        !SDL_SetGPUSwapchainParameters(device->Native(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC)) {
        device->Fail("Cannot configure a linear-output/sRGB swapchain: " + std::string(SDL_GetError()));
        SDL_ReleaseWindowFromGPUDevice(device->Native(), window); SDL_DestroyWindow(window); return nullptr;
    }
    try { return new SurfaceSDL(device, window,handle); }
    catch (...) { SDL_ReleaseWindowFromGPUDevice(device->Native(), window); SDL_DestroyWindow(window); throw; }
}
void SurfaceSDL::Release() {
    delete _Renderer; _Renderer = nullptr;
    if (_Window) {
        SDL_ReleaseWindowFromGPUDevice(_Device->Native(), _Window);
        // SDL's foreign-window wrapper does not own the host native window/view.
        SDL_DestroyWindow(_Window); _Window = nullptr;
    }
}
TextureFormat SurfaceSDL::GetColorFormat() const {
    return _Window ? FromSDL(SDL_GetGPUSwapchainTextureFormat(_Device->Native(), _Window)) : TextureFormat::Unknown;
}
bool SurfaceSDL::Draw(ITexture* texture, const Color& clearColor) {
    _Device->ClearError();
    if (!_Window || !_Device->OnMainThread())
        return _Device->Fail("Draw requires a live surface and its window thread.");
    if (!_Renderer) _Renderer = new TextureRendererSDL(_Device);
    return _Renderer->Draw(*this, texture, clearColor);
}
AcquireFrameResult SurfaceSDL::AcquireFrame(ICommandBuffer* value, SurfaceFrame& frame) {
    frame = {};
    _Device->ClearError();
    auto* command = _Device->GetCommand(value);
    if (!_Window || !_Device->OnMainThread() || !command || !command->OnThread() || command->Active || !command->Error.empty() || command->Surfaces.count(this)) {
        _Device->Fail("Invalid surface, command, thread, active pass or duplicate frame acquisition."); return AcquireFrameResult::Failed;
    }
#if defined(SDL_PLATFORM_MACOS)
    bool drawableAvailable=false;
    std::string syncError;
    if(!SyncCocoaSurfaceDrawable(_HostHandle,_Window,drawableAvailable,syncError)) {
        command->Fail(syncError);return AcquireFrameResult::Failed;
    }
    if(!drawableAvailable)return AcquireFrameResult::Unavailable;
#endif
    // Reserve bookkeeping before acquiring; allocation must not strand an acquired frame.
    command->FrameTokens.reserve(command->FrameTokens.size() + 1);
    command->Surfaces.insert(this);
    uint64_t token = 0;
    try { token = _Device->AddTarget({}); }
    catch (...) { command->Surfaces.erase(this); throw; }
    SDL_GPUTexture* texture = nullptr;
    uint32_t width = 0, height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command->Native, _Window, &texture, &width, &height)) {
        _Device->RemoveTarget(token); command->Surfaces.erase(this);
        command->Fail(SDL_GetError()); return AcquireFrameResult::Failed;
    }
    if (!texture) {
        _Device->RemoveTarget(token); command->Surfaces.erase(this);
        return AcquireFrameResult::Unavailable;
    }
    // Existing registry entry can be filled without further allocation.
    auto* target = const_cast<TargetSDL*>(_Device->FindTarget(token));
    *target = {texture, GetColorFormat(), width, height, command};
    command->FrameTokens.push_back(token);
    frame = {{token}, width, height, target->Format};
    return AcquireFrameResult::Ready;
}
}
