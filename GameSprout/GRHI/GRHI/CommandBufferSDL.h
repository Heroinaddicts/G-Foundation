#pragma once
#include "DeviceSDL.h"
#include <array>
#include <vector>

namespace GRHI {
class RenderPassSDL;
class TextureSDL;
class SamplerSDL;
class PipelineSDL;
class CommandBufferSDL final : public ICommandBuffer {
public:
    CommandBufferSDL(DeviceSDL* owner, SDL_GPUCommandBuffer* native)
        : Owner(owner), Native(native), Thread(std::this_thread::get_id()) {}
    ~CommandBufferSDL() override;
    IRenderPass* BeginRenderPass(const RenderPassDesc&) override;
    void EndRenderPass(IRenderPass*) override;
    bool OnThread() const { return Thread == std::this_thread::get_id(); }
    bool Fail(const std::string& message) { if (Error.empty()) Error = message; return Owner->Fail(Error); }
    DeviceSDL* const Owner;
    SDL_GPUCommandBuffer* Native;
    const std::thread::id Thread;
    RenderPassSDL* Active = nullptr;
    std::string Error;
    // Keep retired pass wrappers until command destruction to avoid reusing a stale pass address.
    std::vector<RenderPassSDL*> Passes;
    std::vector<uint64_t> FrameTokens;
    std::unordered_set<const void*> Surfaces;
};
class RenderPassSDL final : public IRenderPass {
public:
    RenderPassSDL(CommandBufferSDL* command, SDL_GPURenderPass* native, const TargetSDL& target)
        : Command(command), Native(native), Target(target) {}
    void SetPipeline(IGraphicsPipeline*) override;
    void SetViewport(const Viewport&) override;
    void SetFragmentTextureSampler(uint32_t, ITexture*, ISampler*) override;
    void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override;
    bool Usable();
    CommandBufferSDL* const Command;
    SDL_GPURenderPass* Native;
    const TargetSDL Target;
    PipelineSDL* Pipeline = nullptr;
    std::array<TextureSDL*, 16> Textures{};
    std::array<SamplerSDL*, 16> Samplers{};
};
}
