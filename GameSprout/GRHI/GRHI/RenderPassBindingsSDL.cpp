#include "CommandBufferSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace GRHI {
void RenderPassSDL::SetPipeline(IGraphicsPipeline* value) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* pipeline=Command->Owner->Resource<PipelineSDL>(value,ResourceKindSDL::Pipeline);
    if(!pipeline || !pipeline->Native || pipeline->Format!=Target.Format || pipeline->DepthFormat!=Depth.Format) {
        Command->Fail("Pipeline is foreign, released or incompatible with pass color/depth formats.");return;
    }
    const Viewport viewport=State.View;
    State={};State.View=viewport;State.Pipeline={value,pipeline->_Guid};
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
    auto* pipeline=Command->Owner->Resource<PipelineSDL>(State.Pipeline.Pointer,ResourceKindSDL::Pipeline);
    auto* texture=Command->Owner->Resource<TextureSDL>(textureValue,ResourceKindSDL::Texture);
    auto* sampler=Command->Owner->Resource<SamplerSDL>(samplerValue,ResourceKindSDL::Sampler);
    if(!pipeline || pipeline->_Guid!=State.Pipeline.ID || slot>=pipeline->SamplerCount || !texture || !sampler ||
       !texture->Native || !sampler->Native || texture->Native==Target.Texture || texture->Native==Depth.Texture ||
       (texture->Desc.Usage&TextureUsage::Sampled)==TextureUsage::None) {
        Command->Fail("Invalid texture/sampler slot, ownership, released resource or render-target feedback.");return;
    }
    State.Textures[slot]={textureValue,texture->_Guid};State.Samplers[slot]={samplerValue,sampler->_Guid};
}
void RenderPassSDL::SetVertexBuffer(uint32_t binding,IBuffer* value,size_t offset) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* pipeline=Command->Owner->Resource<PipelineSDL>(State.Pipeline.Pointer,ResourceKindSDL::Pipeline);
    auto* buffer=Command->Owner->Resource<BufferSDL>(value,ResourceKindSDL::Buffer);
    const bool declared=pipeline && std::any_of(pipeline->Bindings.begin(),pipeline->Bindings.end(),[binding](const auto& item){return item.Binding==binding;});
    if(!pipeline || pipeline->_Guid!=State.Pipeline.ID || !declared || binding>=16 || !buffer || !buffer->Native ||
       (buffer->Desc.Usage&BufferUsage::Vertex)==BufferUsage::None || offset>=buffer->Desc.Size || offset%4) {
        Command->Fail("Invalid vertex buffer binding, byte offset, usage or device ownership.");return;
    }
    State.Vertices[binding]={{value,buffer->_Guid},offset};
}
void RenderPassSDL::SetIndexBuffer(IBuffer* value,IndexFormat format,size_t offset) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* buffer=Command->Owner->Resource<BufferSDL>(value,ResourceKindSDL::Buffer);
    const size_t unit=format==IndexFormat::UInt16?2:4;
    if(!buffer || !buffer->Native || static_cast<unsigned>(format)>1 || (buffer->Desc.Usage&BufferUsage::Index)==BufferUsage::None ||
       offset>=buffer->Desc.Size || offset%unit) {Command->Fail("Invalid index buffer format, offset, usage or device ownership.");return;}
    State.Index={{value,buffer->_Guid},offset};State.Indices=format;
}
void RenderPassSDL::Uniform(bool vertex,uint32_t slot,const void* data,size_t size) {
    Command->Owner->ClearError();if(!Usable())return;
    auto* pipeline=Command->Owner->Resource<PipelineSDL>(State.Pipeline.Pointer,ResourceKindSDL::Pipeline);
    const uint32_t count=pipeline?(vertex?pipeline->VertexUniformCount:pipeline->FragmentUniformCount):0;
    if(!pipeline || pipeline->_Guid!=State.Pipeline.ID || slot>=count || !data || !size || size>16384 || size%16) {
        Command->Fail("Invalid uniform slot, pointer or std140 byte size (16-byte multiple, at most 16 KiB).");return;
    }
    auto& bytes=vertex?State.VertexUniforms[slot]:State.FragmentUniforms[slot];
    bytes.resize(size);std::memcpy(bytes.data(),data,size);
}
void RenderPassSDL::SetVertexUniform(uint32_t slot,const void* data,size_t size) {Uniform(true,slot,data,size);}
void RenderPassSDL::SetFragmentUniform(uint32_t slot,const void* data,size_t size) {Uniform(false,slot,data,size);}
}
