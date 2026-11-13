#include "CommandBufferSDL.h"
#include "Resource/BufferSDL.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace GRHI {
static std::atomic<uint64_t> NextIdentifier{1};
uint64_t DeviceSDL::NextID() {
    const uint64_t id=NextIdentifier.fetch_add(1,std::memory_order_relaxed);
    if(!id)throw std::overflow_error("GRHI identifier space exhausted");
    return id;
}
uint64_t DeviceSDL::AddTarget(const TargetSDL& target) {
    const auto token=NextID();_Targets.emplace(token,target);return token;
}
const TargetSDL* DeviceSDL::FindTarget(uint64_t token) const {
    const auto found=_Targets.find(token);return found==_Targets.end()?nullptr:&found->second;
}
CommandBufferSDL* DeviceSDL::GetCommand(ICommandBuffer* value) {
    for(auto* command:_Commands)if(static_cast<ICommandBuffer*>(command)==value)return command;
    return nullptr;
}
ICommandBuffer* DeviceSDL::AcquireCommandBuffer() {
    ClearError();
    if(!_Device){Fail("Device has been released.");return nullptr;}
    auto* native=SDL_AcquireGPUCommandBuffer(_Device);
    if(!native){FailSDL();return nullptr;}
    CommandBufferSDL* command=nullptr;
    try {command=new CommandBufferSDL(this,native);_Commands.insert(command);return command;}
    catch(...) {SDL_CancelGPUCommandBuffer(native);delete command;throw;}
}
bool DeviceSDL::Submit(ICommandBuffer* value) {
    ClearError();
    auto* command=GetCommand(value);
    if(!command || !command->OnThread())return Fail("Unknown command or wrong submitting thread.");
    if(command->Active) {
        command->Fail("Cannot submit with an active render pass.");
        command->EndRenderPass(command->Active);
    }
    // No native render/draw calls are encoded until the entire CPU recording has passed validation.
    if(command->Error.empty() && command->Validate())command->Encode();
    const std::string validation=command->Error;
    // SDL cannot cancel a borrowed swapchain image. Invalid recordings submit only the empty
    // acquisition command (or any already valid native passes if the backend itself failed).
    const bool success=!validation.empty() && command->FrameTokens.empty()
        ? SDL_CancelGPUCommandBuffer(command->Native):SDL_SubmitGPUCommandBuffer(command->Native);
    const std::string error=success?std::string{}:std::string(SDL_GetError());
    command->Native=nullptr;_Commands.erase(command);delete command;
    if(!validation.empty())return Fail(validation);
    if(!success)return Fail(error);
    ClearError();return true;
}
bool DeviceSDL::Cancel(ICommandBuffer* value) {
    ClearError();
    auto* command=GetCommand(value);
    if(!command || !command->OnThread())return Fail("Unknown command or wrong cancelling thread.");
    if(command->Active || !command->FrameTokens.empty())return Fail("Cannot cancel an active pass or an acquired window frame.");
    if(!SDL_CancelGPUCommandBuffer(command->Native))return FailSDL();
    command->Native=nullptr;_Commands.erase(command);delete command;return true;
}
CommandBufferSDL::~CommandBufferSDL() {
    for(auto token:FrameTokens)Owner->RemoveTarget(token);
    for(auto* pass:Passes)delete pass;
}
IRenderPass* CommandBufferSDL::BeginRenderPass(const RenderPassDesc& desc) {
    Owner->ClearError();
    if(!OnThread() || Active || !Error.empty()){Fail("Invalid thread, nested pass or failed command.");return nullptr;}
    const auto* color=desc.ColorTarget.Token?Owner->FindTarget(desc.ColorTarget.Token):nullptr;
    const auto* depth=desc.DepthTarget.Token?Owner->FindTarget(desc.DepthTarget.Token):nullptr;
    if(!desc.ColorTarget.Token&&!desc.DepthTarget.Token) {
        Fail("Render pass requires a color or depth target.");return nullptr;
    }
    if(desc.ColorTarget.Token && (!color || color->Format==TextureFormat::D32_Float || (color->Command && color->Command!=this) ||
       static_cast<unsigned>(desc.Load)>2 || static_cast<unsigned>(desc.Store)>1 ||
       (color->Command && desc.Load==LoadOp::Load) || !std::isfinite(desc.ClearColor.R) ||
       !std::isfinite(desc.ClearColor.G) || !std::isfinite(desc.ClearColor.B) || !std::isfinite(desc.ClearColor.A))) {
        Fail("Invalid color target, load/store operation or clear color.");return nullptr;
    }
    if(desc.DepthTarget.Token && (!depth || depth->Command || depth->Format!=TextureFormat::D32_Float ||
       (color && (depth->Width!=color->Width || depth->Height!=color->Height)) || static_cast<unsigned>(desc.DepthLoad)>2 ||
       static_cast<unsigned>(desc.DepthStore)>1 || !std::isfinite(desc.ClearDepth) || desc.ClearDepth<0 || desc.ClearDepth>1)) {
        Fail("Invalid depth target, dimensions, load/store operation or clear depth.");return nullptr;
    }
    auto* pass=new RenderPassSDL(this,desc,color?*color:TargetSDL{},depth?*depth:TargetSDL{});
    try {Passes.push_back(pass);}catch(...){delete pass;throw;}
    Active=pass;return pass;
}
void CommandBufferSDL::EndRenderPass(IRenderPass* value) {
    Owner->ClearError();
    if(!OnThread() || !Active || Active!=value){Fail("Pass is foreign or is no longer active.");return;}
    Active->Ended=true;Active=nullptr;
}
bool CommandBufferSDL::UploadBuffers(const BufferUpload* uploads,uint32_t count) {
    Owner->ClearError();
    if(!OnThread() || Active || !Error.empty())return Fail("Invalid thread, active pass or failed command.");
    if(!count)return true;
    if(!uploads)return Fail("Missing buffer upload descriptions.");

    auto total=_UploadBytes.size();
    for(uint32_t i=0;i<count;++i) {
        const auto& upload=uploads[i];
        const auto* buffer=Owner->Resource<BufferSDL>(upload.Buffer,ResourceKindSDL::Buffer);
        if(!buffer || !buffer->Native || (buffer->Desc.Usage&BufferUsage::Index)!=BufferUsage::None ||
           !upload.Data || !upload.Size || upload.Offset%4 || upload.Size%4 ||
           upload.Offset>buffer->Desc.Size || upload.Size>buffer->Desc.Size-upload.Offset ||
           upload.Size>UINT32_MAX-total)
            return Fail("Buffer upload requires a live non-index buffer, aligned bytes and an in-bounds range.");
        total+=upload.Size;
    }

    auto offset=_UploadBytes.size();
    _Uploads.reserve(_Uploads.size()+count);
    _UploadBytes.resize(total);
    for(uint32_t i=0;i<count;++i) {
        const auto& upload=uploads[i];
        const auto* buffer=Owner->Resource<BufferSDL>(upload.Buffer,ResourceKindSDL::Buffer);
        std::memcpy(_UploadBytes.data()+offset,upload.Data,upload.Size);
        _Uploads.push_back({{upload.Buffer,buffer->_Guid},Passes.size(),static_cast<uint32_t>(upload.Offset),
            static_cast<uint32_t>(offset),static_cast<uint32_t>(upload.Size),upload.Cycle});
        offset+=upload.Size;
    }
    return true;
}
bool RenderPassSDL::Usable() {
    if(!Command->OnThread() || Command->Active!=this || Ended)return Command->Fail("Inactive pass or wrong recording thread.");
    if(!Command->Error.empty()){Command->Owner->Fail(Command->Error);return false;}
    return ValidateTargets();
}
bool RenderPassSDL::ValidateTargets() {
    const auto revision=Command->Owner->TargetReleaseRevision();
    if(revision==_TargetRevision)return true;
    const auto* color=Desc.ColorTarget.Token?Command->Owner->FindTarget(Desc.ColorTarget.Token):nullptr;
    const auto* depth=Desc.DepthTarget.Token?Command->Owner->FindTarget(Desc.DepthTarget.Token):nullptr;
    if((Desc.ColorTarget.Token && (!color || color->Texture!=Target.Texture)) ||
       (Desc.DepthTarget.Token && (!depth || depth->Texture!=Depth.Texture)))
        return Command->Fail("A render target was released before command submission.");
    _TargetRevision=revision;
    return true;
}
bool CommandBufferSDL::Validate() {
    const bool released=ResourceRevision!=Owner->ResourceReleaseRevision();
    if(released)for(const auto& upload:_Uploads)
        if(!Owner->IsResourceLive(upload.Destination.Pointer,upload.Destination.ID))
            return Fail("A buffer upload destination was released before command submission.");
    for(auto* pass:Passes) {
        if(!pass->Ended || !pass->ValidateTargets())return Fail("Unfinished pass or released render target.");
        if(released&&!pass->ValidateResources())return false;
    }
    return true;
}
}
