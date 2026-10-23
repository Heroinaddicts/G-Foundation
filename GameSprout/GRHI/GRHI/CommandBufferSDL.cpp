#include "CommandBufferSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include "Resource/PipelineSDL.h"
#include <cmath>
#include <atomic>
#include <stdexcept>

namespace GRHI {
// Unique tokens across devices prevent a foreign target from aliasing a local target.
static std::atomic<uint64_t> s_NextID{1};
uint64_t DeviceSDL::NextID() {
    const uint64_t id = s_NextID.fetch_add(1, std::memory_order_relaxed);
    if (!id) throw std::overflow_error("GRHI identifier space exhausted");
    return id;
}
uint64_t DeviceSDL::AddTarget(const TargetSDL& target) {
    const auto token = NextID(); _Targets.emplace(token, target); return token;
}
const TargetSDL* DeviceSDL::FindTarget(uint64_t token) const {
    const auto it = _Targets.find(token); return it == _Targets.end() ? nullptr : &it->second;
}
CommandBufferSDL* DeviceSDL::GetCommand(ICommandBuffer* command) {
    // Membership is checked before dereferencing, so already-consumed commands are rejected.
    for (auto* candidate : _Commands) if (static_cast<ICommandBuffer*>(candidate) == command) return candidate;
    return nullptr;
}
ICommandBuffer* DeviceSDL::AcquireCommandBuffer() {
    ClearError();
    if (!_Device) { Fail("Device has been released."); return nullptr; }
    auto* native = SDL_AcquireGPUCommandBuffer(_Device);
    if (!native) { FailSDL(); return nullptr; }
    CommandBufferSDL* command = nullptr;
    try {
        command = new CommandBufferSDL(this, native);
        _Commands.insert(command);
        return command;
    } catch (...) {
        SDL_CancelGPUCommandBuffer(native);
        delete command;
        throw;
    }
}
bool DeviceSDL::Submit(ICommandBuffer* value) {
    ClearError();
    auto* command = GetCommand(value);
    if (!command || !command->OnThread()) return Fail("Unknown command or wrong submitting thread.");
    if (command->Active) {
        command->Fail("Cannot submit with an active render pass.");
        command->EndRenderPass(command->Active);
    }
    const std::string validation = command->Error;
    // SDL cannot cancel after a swapchain acquisition; submit to release/present that frame.
    const bool success = !validation.empty() && command->FrameTokens.empty()
        ? SDL_CancelGPUCommandBuffer(command->Native) : SDL_SubmitGPUCommandBuffer(command->Native);
    const std::string error = success ? std::string{} : std::string(SDL_GetError());
    command->Native = nullptr;
    _Commands.erase(command);
    delete command;
    if (!validation.empty()) return Fail(validation);
    if (!success) return Fail(error);
    ClearError(); return true;
}
bool DeviceSDL::Cancel(ICommandBuffer* value) {
    ClearError();
    auto* command = GetCommand(value);
    if (!command || !command->OnThread()) return Fail("Unknown command or wrong cancelling thread.");
    if (command->Active || !command->FrameTokens.empty()) return Fail("Cannot cancel an active pass or an acquired window frame.");
    if (!SDL_CancelGPUCommandBuffer(command->Native)) return FailSDL();
    command->Native = nullptr;
    _Commands.erase(command); delete command;
    return true;
}
CommandBufferSDL::~CommandBufferSDL() {
    for (auto token : FrameTokens) Owner->RemoveTarget(token);
    for (auto* pass : Passes) delete pass;
}
IRenderPass* CommandBufferSDL::BeginRenderPass(const RenderPassDesc& desc) {
    Owner->ClearError();
    if (!OnThread() || Active || !Error.empty()) { Fail("Invalid thread, nested pass or failed command."); return nullptr; }
    const auto* target = Owner->FindTarget(desc.ColorTarget.Token);
    if (!target || (target->Command && target->Command != this) ||
        static_cast<unsigned>(desc.Load) > 2 || static_cast<unsigned>(desc.Store) > 1 ||
        (target->Command && desc.Load == LoadOp::Load) ||
        !std::isfinite(desc.ClearColor.R) || !std::isfinite(desc.ClearColor.G) ||
        !std::isfinite(desc.ClearColor.B) || !std::isfinite(desc.ClearColor.A)) {
        Fail("Invalid color target, load/store operation or clear color."); return nullptr;
    }
    // Allocate bookkeeping before beginning native recording; allocation failures leave no active native pass.
    auto* pass = new RenderPassSDL(this, nullptr, *target);
    try { Passes.push_back(pass); } catch (...) { delete pass; throw; }
    SDL_GPUColorTargetInfo color{};
    color.texture = target->Texture;
    const SDL_GPULoadOp loads[] = {SDL_GPU_LOADOP_CLEAR, SDL_GPU_LOADOP_LOAD, SDL_GPU_LOADOP_DONT_CARE};
    color.load_op = loads[static_cast<unsigned>(desc.Load)];
    color.store_op = desc.Store == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    color.clear_color = {desc.ClearColor.R, desc.ClearColor.G, desc.ClearColor.B, desc.ClearColor.A};
    pass->Native = SDL_BeginGPURenderPass(Native, &color, 1, nullptr);
    if (!pass->Native) { Fail(SDL_GetError()); return nullptr; }
    Active = pass;
    return pass;
}
void CommandBufferSDL::EndRenderPass(IRenderPass* value) {
    Owner->ClearError();
    if (!OnThread() || !Active || Active != value) { Fail("Pass does not belong to this command or is no longer active."); return; }
    SDL_EndGPURenderPass(Active->Native);
    Active->Native = nullptr; Active = nullptr;
}
bool RenderPassSDL::Usable() {
    if (!Command->OnThread() || Command->Active != this || !Native) return Command->Fail("Inactive render pass or wrong recording thread.");
    if (!Command->Error.empty()) { Command->Owner->Fail(Command->Error); return false; }
    return true;
}
void RenderPassSDL::SetPipeline(IGraphicsPipeline* value) {
    Command->Owner->ClearError();
    if (!Usable()) return;
    auto* pipeline = dynamic_cast<PipelineSDL*>(value);
    if (!pipeline || pipeline->Owner != Command->Owner || !pipeline->Native || pipeline->Format != Target.Format) {
        Command->Fail("Pipeline is invalid, foreign or incompatible with the color target."); return;
    }
    Pipeline = pipeline;
    // Require bindings for the newly selected pipeline; Draw rebinds the complete set.
    Textures.fill(nullptr); Samplers.fill(nullptr);
    SDL_BindGPUGraphicsPipeline(Native, pipeline->Native);
}
void RenderPassSDL::SetViewport(const Viewport& value) {
    Command->Owner->ClearError();
    if (!Usable()) return;
    if (!std::isfinite(value.X) || !std::isfinite(value.Y) || !std::isfinite(value.Width) || !std::isfinite(value.Height) ||
        !std::isfinite(value.MinDepth) || !std::isfinite(value.MaxDepth) || value.Width <= 0 || value.Height <= 0 ||
        value.MinDepth < 0 || value.MaxDepth > 1 || value.MinDepth > value.MaxDepth) {
        Command->Fail("Invalid viewport dimensions or depth range."); return;
    }
    SDL_GPUViewport viewport{value.X, value.Y, value.Width, value.Height, value.MinDepth, value.MaxDepth};
    SDL_SetGPUViewport(Native, &viewport);
}
void RenderPassSDL::SetFragmentTextureSampler(uint32_t slot, ITexture* textureValue, ISampler* samplerValue) {
    Command->Owner->ClearError();
    if (!Usable()) return;
    auto* texture = dynamic_cast<TextureSDL*>(textureValue);
    auto* sampler = dynamic_cast<SamplerSDL*>(samplerValue);
    if (!Pipeline || slot >= Pipeline->SamplerCount || !texture || !sampler || !texture->Native || !sampler->Native ||
        texture->Owner != Command->Owner || sampler->Owner != Command->Owner || texture->Native == Target.Texture ||
        (texture->Desc.Usage & TextureUsage::Sampled) == TextureUsage::None) {
        Command->Fail("Invalid texture/sampler binding, slot, ownership or render-target feedback."); return;
    }
    Textures[slot] = texture; Samplers[slot] = sampler;
}
void RenderPassSDL::Draw(uint32_t count, uint32_t instances, uint32_t first, uint32_t firstInstance) {
    Command->Owner->ClearError();
    if (!Usable()) return;
    if (!Pipeline || !Pipeline->Native || !count || !instances || first > UINT32_MAX - count || firstInstance > UINT32_MAX - instances) {
        Command->Fail("Draw requires a valid pipeline and vertex/instance ranges."); return;
    }
    SDL_GPUTextureSamplerBinding bindings[16]{};
    for (uint32_t slot = 0; slot < Pipeline->SamplerCount; ++slot) {
        if (!Textures[slot] || !Samplers[slot] || !Textures[slot]->Native || !Samplers[slot]->Native) {
            Command->Fail("Draw is missing a required texture/sampler."); return;
        }
        bindings[slot] = {Textures[slot]->Native, Samplers[slot]->Native};
    }
    if (Pipeline->SamplerCount) SDL_BindGPUFragmentSamplers(Native, 0, bindings, Pipeline->SamplerCount);
    SDL_DrawGPUPrimitives(Native, count, instances, first, firstInstance);
}
}
