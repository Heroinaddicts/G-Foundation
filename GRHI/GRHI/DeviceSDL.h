#ifndef __DeviceSDL_h__
#define __DeviceSDL_h__
#include "GRHIApi.h"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <unordered_set>
#include <thread>

namespace GRHI {
    class CommandBufferSDL;
    struct TargetSDL {
        SDL_GPUTexture* Texture = nullptr;
        TextureFormat Format = TextureFormat::Unknown;
        uint32_t Width = 0, Height = 0;
        CommandBufferSDL* Command = nullptr; // Non-null for borrowed swapchain targets.
    };
    class DeviceSDL final : public IDevice {
    public:
        ~DeviceSDL() override;
        DeviceSDL(const DeviceSDL&) = delete;
        DeviceSDL& operator=(const DeviceSDL&) = delete;
        static DeviceSDL* CreateDeviceSDL(const std::string&, std::string&);
        void Release() override;
        ISurface* CreateSurface(GRHIWindowHandle handle) override;
        ITexture* CreateTexture(const TextureDesc&, const TextureInitialData*) override;
        ISampler* CreateSampler(const SamplerDesc&) override;
        IShader* CreateShader(const ShaderDesc&) override;
        IGraphicsPipeline* CreateGraphicsPipeline(const GraphicsPipelineDesc&) override;
        ICommandBuffer* AcquireCommandBuffer() override;
        bool Submit(ICommandBuffer*) override;
        bool Cancel(ICommandBuffer*) override;
        const std::string& GetLastError() const override { return _Error; }

        SDL_GPUDevice* Native() const { return _Device; }
        bool Fail(const std::string& message) { _Error = message; return false; }
        bool FailSDL() { return Fail(SDL_GetError()); }
        void ClearError() { _Error.clear(); }
        uint64_t NextID();
        uint64_t AddTarget(const TargetSDL& target);
        void RemoveTarget(uint64_t token) { _Targets.erase(token); }
        const TargetSDL* FindTarget(uint64_t token) const;
        CommandBufferSDL* GetCommand(ICommandBuffer* command);
        bool OnMainThread() const { return _MainThread == std::this_thread::get_id(); }
    private:
        explicit DeviceSDL(SDL_GPUDevice* device) : _Device(device), _MainThread(std::this_thread::get_id()) {}
        SDL_GPUDevice* _Device = nullptr;
        std::string _Error;
        std::unordered_map<uint64_t, TargetSDL> _Targets;
        std::unordered_set<CommandBufferSDL*> _Commands;
        std::thread::id _MainThread;
    };
    SDL_GPUTextureFormat ToSDL(TextureFormat format);
    TextureFormat FromSDL(SDL_GPUTextureFormat format);
}
#endif
