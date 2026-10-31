#pragma once
#include "DeviceSDL.h"
#include <array>
#include <vector>

namespace GRHI {
class RenderPassSDL;
struct ResourceBindingSDL { const void* Pointer = nullptr; uint64_t ID = 0; };
struct BufferBindingSDL { ResourceBindingSDL Resource; size_t Offset = 0; };
struct DrawRecordSDL {
    ResourceBindingSDL Pipeline;
    std::array<BufferBindingSDL,16> Vertices{};
    BufferBindingSDL Index;
    IndexFormat Indices = IndexFormat::UInt32;
    std::array<ResourceBindingSDL,16> Textures{},Samplers{};
    std::array<std::vector<uint8_t>,4> VertexUniforms,FragmentUniforms;
    Viewport View;
    bool Indexed = false;
    uint32_t Count=0,Instances=1,First=0,FirstInstance=0;
    int32_t VertexOffset=0;
};
class CommandBufferSDL final : public ICommandBuffer {
public:
    CommandBufferSDL(DeviceSDL* owner,SDL_GPUCommandBuffer* native)
        : Owner(owner),Native(native),Thread(std::this_thread::get_id()) {}
    ~CommandBufferSDL() override;
    IRenderPass* BeginRenderPass(const RenderPassDesc&) override;
    void EndRenderPass(IRenderPass*) override;
    bool OnThread() const { return Thread==std::this_thread::get_id(); }
    bool Fail(const std::string& message) { if(Error.empty())Error=message;return Owner->Fail(Error); }
    bool Validate();
    bool Encode();
    DeviceSDL* const Owner;
    SDL_GPUCommandBuffer* Native;
    const std::thread::id Thread;
    RenderPassSDL* Active=nullptr;
    std::string Error;
    std::vector<RenderPassSDL*> Passes;
    std::vector<uint64_t> FrameTokens;
    std::unordered_set<const void*> Surfaces;
};
class RenderPassSDL final : public IRenderPass {
public:
    RenderPassSDL(CommandBufferSDL* command,const RenderPassDesc& desc,const TargetSDL& target,const TargetSDL& depth)
        : Command(command),Desc(desc),Target(target),Depth(depth) { State.View.Width=float(target.Width);State.View.Height=float(target.Height); }
    void SetPipeline(IGraphicsPipeline*) override;
    void SetViewport(const Viewport&) override;
    void SetFragmentTextureSampler(uint32_t,ITexture*,ISampler*) override;
    void SetVertexBuffer(uint32_t,IBuffer*,size_t) override;
    void SetIndexBuffer(IBuffer*,IndexFormat,size_t) override;
    void SetVertexUniform(uint32_t,const void*,size_t) override;
    void SetFragmentUniform(uint32_t,const void*,size_t) override;
    void Draw(uint32_t,uint32_t,uint32_t,uint32_t) override;
    void DrawIndexed(uint32_t,uint32_t,uint32_t,int32_t,uint32_t) override;
    bool Usable();
    bool ValidateTargets();
    bool ValidateDraw(const DrawRecordSDL&);
    void Uniform(bool vertex,uint32_t slot,const void* data,size_t size);
    CommandBufferSDL* const Command;
    const RenderPassDesc Desc;
    const TargetSDL Target,Depth;
    bool Ended=false;
    DrawRecordSDL State;
    std::vector<DrawRecordSDL> Draws;
};
}
