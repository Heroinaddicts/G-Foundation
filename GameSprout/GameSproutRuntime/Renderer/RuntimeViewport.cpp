#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "Api/GameSproutRuntimeExport.h"
#include "GRHIExport.h"
#include <filesystem>
#include <limits>

namespace GameSproutRuntime {
namespace {
struct GraphicsApiOwner {
    GRHI::GRHIApi* Api=nullptr;
    ~GraphicsApiOwner(){delete Api;}
};
std::string LibraryDirectory() {
#if defined(_WIN32)
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CreateGameSproutRuntime),&module))return {};
    std::wstring path(32768,L'\0');
    const DWORD length=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
    if(!length||length==path.size())return {};
    path.resize(length);const auto utf8=std::filesystem::path(path).parent_path().u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());
#else
    Dl_info image{};
    if(!dladdr(reinterpret_cast<void*>(&CreateGameSproutRuntime),&image)||!image.dli_fname)return {};
    return std::filesystem::absolute(image.dli_fname).parent_path().string();
#endif
}
}
GRHI::IDevice* Runtime::RenderDevice(std::string& error) {
    error.clear();
    if(IsExecuting()||IsShuttingDown()){error="Cannot create a viewport while Runtime is executing";return nullptr;}
    if(_RenderDevice)return _RenderDevice;
    // ExportGRHIApi shares one API instance. A process owner releases it once,
    // after all Runtime instances have released their own shared-view device.
    static GraphicsApiOwner owner;
    if(!owner.Api)owner.Api=ExportGRHIApi(LibraryDirectory());
    _RenderApi=owner.Api;
    if(!_RenderApi){error="Cannot load the Runtime graphics backend beside the Runtime library";return nullptr;}
    _RenderDevice=_RenderApi->CreateDevice();
    if(!_RenderDevice)error=_RenderApi->GetLastError();
    return _RenderDevice;
}
void Runtime::ReleaseGraphics() {
    if(_RenderDevice){_RenderDevice->Release();delete _RenderDevice;_RenderDevice=nullptr;}
    _RenderApi=nullptr;
}
IRuntimeViewport* Runtime::CreateViewport(const NativeWindow& window,IRuntimeResourceProvider* resources,std::string& error) {
    error.clear();
    if(!resources||!window.Handle){error="A viewport requires a resource provider and a native window";return nullptr;}
#if defined(__APPLE__)
    const bool supported=window.System==NativeWindowSystem::Cocoa;
#elif defined(_WIN32)
    const bool supported=window.System==NativeWindowSystem::Win32;
#else
    const bool supported=window.System==NativeWindowSystem::X11;
#endif
    if(!supported){error="The native window system is unsupported by the current Runtime backend";return nullptr;}
    auto* device=RenderDevice(error);if(!device)return nullptr;
#if defined(__APPLE__)||defined(_WIN32)
    const auto handle=reinterpret_cast<GRHI::GRHIWindowHandle>(window.Handle);
#else
    const auto handle=static_cast<GRHI::GRHIWindowHandle>(window.Handle);
#endif
    auto* surface=device->CreateSurface(handle);
    if(!surface){error=device->GetLastError();return nullptr;}
    try {
        auto* renderer=new Renderer(*this,*device,*resources);renderer->BindSurface(surface);return renderer;
    }catch(...){surface->Release();delete surface;error="Cannot allocate the Runtime viewport";return nullptr;}
}
IRuntimeViewport* Runtime::CreateOffscreenRenderer(std::uint32_t width,std::uint32_t height,IRuntimeResourceProvider* resources,std::string& error) {
    error.clear();
    if(!resources){error="An offscreen viewport requires a resource provider";return nullptr;}
    auto* device=RenderDevice(error);if(!device)return nullptr;
    Renderer* renderer=nullptr;
    try {
        renderer=new Renderer(*this,*device,*resources);
        if(!renderer->Resize(width,height,error)){delete renderer;return nullptr;}
        return renderer;
    }catch(...){delete renderer;error="Cannot allocate the Runtime offscreen viewport";return nullptr;}
}
bool Renderer::Resize(std::uint32_t width,std::uint32_t height,std::string& error) {
    error.clear();
    if(_Rendering||_Runtime.IsExecuting()||_Runtime.IsShuttingDown()||_Surface){error="Only an idle offscreen viewport can be resized";return false;}
    if(std::uint64_t(width)*height>128u*1024u*1024u){error="Offscreen pixel extent exceeds the Runtime limit";return false;}
    GRHI::ITexture* next=nullptr;
    if(width&&height){
        GRHI::TextureDesc description;description.Width=width;description.Height=height;description.Usage=GRHI::TextureUsage::ColorTarget;
        next=_Device.CreateTexture(description);
        if(!next){error=_Device.GetLastError();return false;}
    }
    delete _ColorTarget;_ColorTarget=next;_Statistics={};_RenderedCamera={};_HasRenderedCamera=false;return true;
}
bool Renderer::ReadPixels(std::vector<std::uint8_t>& pixels,std::string& error) {
    pixels.clear();error.clear();
    if(_Rendering||_Runtime.IsExecuting()||_Runtime.IsShuttingDown()||_Surface||!_ColorTarget){error="Readback requires an idle offscreen viewport with a nonzero extent";return false;}
    if(!_Device.ReadTexture(_ColorTarget,pixels)){pixels.clear();error=_Device.GetLastError();return false;}
    return true;
}
}
