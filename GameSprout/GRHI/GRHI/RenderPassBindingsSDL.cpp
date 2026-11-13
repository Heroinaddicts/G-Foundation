#include "CommandBufferSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include <cmath>
#include <cstring>

namespace GRHI {
PipelineSDL* RenderPassSDL::CurrentPipeline() {
    auto* owner=Command->Owner;
    const auto revision=owner->ResourceReleaseRevision();
    if(revision!=_PipelineRevision) {
        if(!owner->IsResourceLive(State.Pipeline.Pointer,State.Pipeline.ID))State.Pipeline={};
        _PipelineRevision=revision;
    }
    return static_cast<PipelineSDL*>(State.Pipeline.Implementation);
}
void RenderPassSDL::SetPipeline(IGraphicsPipeline* value) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* owner=Command->Owner;
    auto* pipeline=State.Pipeline.Pointer==value?CurrentPipeline():nullptr;
    if(!pipeline)pipeline=owner->Resource<PipelineSDL>(value,ResourceKindSDL::Pipeline);
    if(!pipeline || !pipeline->Native || pipeline->Format!=Target.Format || pipeline->DepthFormat!=Depth.Format) {
        Command->Fail("Pipeline is foreign, released or incompatible with pass color/depth formats.");return;
    }
    // Invalidate required bindings without copying or clearing the slot arrays.
    // The masks enforce the SetPipeline contract even when the pipeline repeats.
    State.Pipeline={value,pipeline->_Guid,pipeline};
    _PipelineRevision=owner->ResourceReleaseRevision();
    _VertexBound=_SamplerBound=_VertexStorageBound=_VertexUniformBound=_FragmentUniformBound=0;
    _IndexBound=false;
}
void RenderPassSDL::SetViewport(const Viewport& v) {
    Command->Owner->ClearError();if(!Usable())return;
    if(!std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Width) || !std::isfinite(v.Height) ||
       !std::isfinite(v.MinDepth) || !std::isfinite(v.MaxDepth) || v.Width<=0 || v.Height<=0 ||
       v.MinDepth<0 || v.MaxDepth>1 || v.MinDepth>v.MaxDepth) {Command->Fail("Invalid viewport dimensions or depth range.");return;}
    State.View=v;
}
void RenderPassSDL::SetFragmentTextureSampler(uint32_t slot,ITexture* textureValue,ISampler* samplerValue) {
    Command->Owner->ClearError();if(!Usable())return;
    const auto* pipeline=CurrentPipeline();
    auto* texture=Command->Owner->Resource<TextureSDL>(textureValue,ResourceKindSDL::Texture);
    auto* sampler=Command->Owner->Resource<SamplerSDL>(samplerValue,ResourceKindSDL::Sampler);
    if(!pipeline || slot>=pipeline->SamplerCount || !texture || !sampler ||
       !texture->Native || !sampler->Native || texture->Native==Target.Texture || texture->Native==Depth.Texture ||
       (texture->Desc.Usage&TextureUsage::Sampled)==TextureUsage::None) {
        Command->Fail("Invalid texture/sampler slot, ownership, released resource or render-target feedback.");return;
    }
    State.Textures[slot]={textureValue,texture->_Guid,texture};
    State.Samplers[slot]={samplerValue,sampler->_Guid,sampler};
    _SamplerBound|=1u<<slot;
}
void RenderPassSDL::SetVertexBuffer(uint32_t binding,IBuffer* value,size_t offset) {
    Command->Owner->ClearError();if(!Usable())return;
    const auto* pipeline=CurrentPipeline();
    auto* buffer=Command->Owner->Resource<BufferSDL>(value,ResourceKindSDL::Buffer);
    if(!pipeline || binding>=16 || !(pipeline->BindingMask&(1u<<binding)) || !buffer || !buffer->Native ||
       (buffer->Desc.Usage&BufferUsage::Vertex)==BufferUsage::None || offset>=buffer->Desc.Size || offset%4) {
        Command->Fail("Invalid vertex buffer binding, byte offset, usage or device ownership.");return;
    }
    State.Vertices[binding]={{value,buffer->_Guid,buffer},offset};
    _VertexBound|=1u<<binding;
}
void RenderPassSDL::SetVertexStorageBuffer(uint32_t slot,IBuffer* value) {
    Command->Owner->ClearError();if(!Usable())return;
    const auto* pipeline=CurrentPipeline();
    auto* buffer=Command->Owner->Resource<BufferSDL>(value,ResourceKindSDL::Buffer);
    if(!pipeline || slot>=pipeline->VertexStorageCount || !buffer || !buffer->Native ||
       (buffer->Desc.Usage&BufferUsage::VertexStorage)==BufferUsage::None) {
        Command->Fail("Invalid vertex storage slot, buffer usage or device ownership.");return;
    }
    State.VertexStorage[slot]={value,buffer->_Guid,buffer};
    _VertexStorageBound|=1u<<slot;
}
void RenderPassSDL::SetIndexBuffer(IBuffer* value,IndexFormat format,size_t offset) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* buffer=Command->Owner->Resource<BufferSDL>(value,ResourceKindSDL::Buffer);
    const size_t unit=format==IndexFormat::UInt16?2:4;
    if(!buffer || !buffer->Native || static_cast<unsigned>(format)>1 || (buffer->Desc.Usage&BufferUsage::Index)==BufferUsage::None ||
       offset>=buffer->Desc.Size || offset%unit) {Command->Fail("Invalid index buffer format, offset, usage or device ownership.");return;}
    State.Index={{value,buffer->_Guid,buffer},offset};State.Indices=format;
    _IndexBound=true;
}
void RenderPassSDL::Uniform(bool vertex,uint32_t slot,const void* data,size_t size) {
    Command->Owner->ClearError();if(!Usable())return;
    const auto* pipeline=CurrentPipeline();
    const uint32_t count=pipeline?(vertex?pipeline->VertexUniformCount:pipeline->FragmentUniformCount):0;
    if(!pipeline || slot>=count || !data || !size || size>16384 || size%16) {
        Command->Fail("Invalid uniform slot, pointer or std140 byte size (16-byte multiple, at most 16 KiB).");return;
    }
    auto& bytes=vertex?State.VertexUniforms[slot]:State.FragmentUniforms[slot];
    auto& bound=vertex?_VertexUniformBound:_FragmentUniformBound;
    bound|=1u<<slot;
    if(bytes && bytes->size()==size && std::memcmp(bytes->data(),data,size)==0)return;
    bytes=Uniforms.Store(data,size);
}
void RenderPassSDL::SetVertexUniform(uint32_t slot,const void* data,size_t size) {Uniform(true,slot,data,size);}
void RenderPassSDL::SetFragmentUniform(uint32_t slot,const void* data,size_t size) {Uniform(false,slot,data,size);}
}
