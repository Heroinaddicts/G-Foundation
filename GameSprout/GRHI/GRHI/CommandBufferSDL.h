#pragma once
#include "DeviceSDL.h"
#include "ReplayStateSDL.h"
#include "UniformArenaSDL.h"
#include <variant>
#include <vector>

namespace GRHI {
class RenderPassSDL;
class BufferSDL;
class PipelineSDL;
struct BufferUploadSDL {
    ResourceBindingSDL Destination;
    size_t BeforePass = 0;
    uint32_t DestinationOffset = 0, SourceOffset = 0, Size = 0;
    bool Cycle = false;
};
struct BindPipelineSDL { SDL_GPUGraphicsPipeline* Pipeline; };
struct BindViewportSDL { SDL_GPUViewport View; };
struct BindVertexSDL { uint32_t Slot; SDL_GPUBufferBinding Binding; };
struct BindIndexSDL { SDL_GPUBufferBinding Binding; SDL_GPUIndexElementSize Format; };
struct BindVertexStorageSDL { uint32_t Slot; SDL_GPUBuffer* Buffer; };
struct BindUniformSDL { bool Vertex; uint32_t Slot; const std::vector<uint8_t>* Bytes; };
struct BindSamplersSDL { size_t First; uint32_t Count; };
struct NativeDrawSDL {
    bool Indexed;
    uint32_t Count,Instances,First,FirstInstance;
    int32_t VertexOffset;
};
using PassOperationSDL=std::variant<BindPipelineSDL,BindViewportSDL,BindVertexSDL,BindIndexSDL,
    BindVertexStorageSDL,BindUniformSDL,BindSamplersSDL,NativeDrawSDL>;

class CommandBufferSDL final : public ICommandBuffer {
public:
    CommandBufferSDL(DeviceSDL* owner,SDL_GPUCommandBuffer* native)
        : Owner(owner),Native(native),Thread(std::this_thread::get_id()),ResourceRevision(owner->ResourceReleaseRevision()) {}
    ~CommandBufferSDL() override;
    IRenderPass* BeginRenderPass(const RenderPassDesc&) override;
    void EndRenderPass(IRenderPass*) override;
    bool UploadBuffers(const BufferUpload*,uint32_t) override;
    bool OnThread() const { return Thread==std::this_thread::get_id(); }
    bool Fail(const std::string& message) { if(Error.empty())Error=message;return Owner->Fail(Error); }
    bool Validate();
    bool Encode();
    DeviceSDL* const Owner;
    SDL_GPUCommandBuffer* Native;
    const std::thread::id Thread;
    const uint64_t ResourceRevision;
    RenderPassSDL* Active=nullptr;
    std::string Error;
    std::vector<RenderPassSDL*> Passes;
    std::vector<uint64_t> FrameTokens;
    std::unordered_set<const void*> Surfaces;
private:
    std::vector<BufferUploadSDL> _Uploads;
    std::vector<uint8_t> _UploadBytes;
};
class RenderPassSDL final : public IRenderPass {
public:
    RenderPassSDL(CommandBufferSDL* command,const RenderPassDesc& desc,const TargetSDL& target,const TargetSDL& depth)
        : Command(command),Desc(desc),Target(target),Depth(depth),
          _TargetRevision(command->Owner->TargetReleaseRevision()),
          _ResourceRevision(command->Owner->ResourceReleaseRevision()),_PipelineRevision(_ResourceRevision) {
        State.View.Width=float(target.Texture?target.Width:depth.Width);
        State.View.Height=float(target.Texture?target.Height:depth.Height);
    }
    void SetPipeline(IGraphicsPipeline*) override;
    void SetViewport(const Viewport&) override;
    void SetFragmentTextureSampler(uint32_t,ITexture*,ISampler*) override;
    void SetVertexBuffer(uint32_t,IBuffer*,size_t) override;
    void SetVertexStorageBuffer(uint32_t,IBuffer*) override;
    void SetIndexBuffer(IBuffer*,IndexFormat,size_t) override;
    void SetVertexUniform(uint32_t,const void*,size_t) override;
    void SetFragmentUniform(uint32_t,const void*,size_t) override;
    void Draw(uint32_t,uint32_t,uint32_t,uint32_t) override;
    void DrawIndexed(uint32_t,uint32_t,uint32_t,int32_t,uint32_t) override;
    bool Usable();
    bool ValidateTargets();
    bool ValidateResources() const;
    void Encode(SDL_GPURenderPass*) const;
    void Uniform(bool vertex,uint32_t slot,const void* data,size_t size);
    CommandBufferSDL* const Command;
    const RenderPassDesc Desc;
    const TargetSDL Target,Depth;
    bool Ended=false;
private:
    void RecordDraw(const NativeDrawSDL&);
    bool ValidateDraw(const NativeDrawSDL&,const PipelineSDL&);
    bool RefreshBindings();
    PipelineSDL* CurrentPipeline();
    PassBindingsSDL State;
    uint32_t _VertexBound=0,_SamplerBound=0,_VertexStorageBound=0;
    uint32_t _VertexUniformBound=0,_FragmentUniformBound=0;
    bool _IndexBound=false;
    UniformArenaSDL Uniforms;
    ReplayStateSDL _Replay;
    std::vector<PassOperationSDL> _Operations;
    std::vector<SDL_GPUTextureSamplerBinding> _NativeSamplers;
    std::vector<ResourceBindingSDL> _RecordedResources;
    uint64_t _TargetRevision,_ResourceRevision,_PipelineRevision;
    BufferSDL* _LastIndexBuffer=nullptr;
    uint64_t _LastIndexID=0;
    size_t _LastIndexByteOffset=0;
    uint32_t _LastIndexCount=0;
    IndexFormat _LastIndexFormat=IndexFormat::UInt32;
    uint32_t _LastIndexMinimum=0,_LastIndexMaximum=0;
};
}
