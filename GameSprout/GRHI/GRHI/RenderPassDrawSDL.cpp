#include "CommandBufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"

namespace GRHI {
bool RenderPassSDL::ValidateDraw(const DrawRecordSDL& draw) {
    auto* owner=Command->Owner;
    auto* pipeline=owner->Resource<PipelineSDL>(draw.Pipeline.Pointer,ResourceKindSDL::Pipeline);
    if(!pipeline || pipeline->_Guid!=draw.Pipeline.ID || !pipeline->Native || !draw.Count || !draw.Instances ||
       draw.First>UINT32_MAX-draw.Count || draw.FirstInstance>UINT32_MAX-draw.Instances)
        return Command->Fail("Draw requires live pipeline and nonoverflowing vertex/index/instance ranges.");
    for(uint32_t slot=0;slot<pipeline->SamplerCount;++slot) {
        if(!owner->IsResourceLive(draw.Textures[slot].Pointer,draw.Textures[slot].ID) ||
           !owner->IsResourceLive(draw.Samplers[slot].Pointer,draw.Samplers[slot].ID))return Command->Fail("Draw texture or sampler is missing or was released.");
    }
    for(uint32_t slot=0;slot<pipeline->VertexUniformCount;++slot)
        if(!draw.VertexUniforms[slot] || draw.VertexUniforms[slot]->empty())return Command->Fail("Draw is missing a vertex uniform binding.");
    for(uint32_t slot=0;slot<pipeline->FragmentUniformCount;++slot)
        if(!draw.FragmentUniforms[slot] || draw.FragmentUniforms[slot]->empty())return Command->Fail("Draw is missing a fragment uniform binding.");
    uint64_t maximum=uint64_t(draw.First)+draw.Count-1;
    if(draw.Indexed) {
        auto* indices=owner->Resource<BufferSDL>(draw.Index.Resource.Pointer,ResourceKindSDL::Buffer);
        const uint64_t unit=draw.Indices==IndexFormat::UInt16?2:4;
        if(!indices || indices->_Guid!=draw.Index.Resource.ID || !indices->Native ||
           draw.Index.Offset>indices->Desc.Size || (uint64_t(draw.First)+draw.Count)*unit>indices->Desc.Size-draw.Index.Offset)
            return Command->Fail("DrawIndexed index span is missing, released or out of bounds.");
        IndexRangeCacheSDL::Range range;
        const auto byteOffset=draw.Index.Offset+uint64_t(draw.First)*unit;
        if(!indices->GetIndexRange(byteOffset,draw.Count,draw.Indices,range))
            return Command->Fail("DrawIndexed index format, alignment or immutable byte range is invalid.");
        // The cached extrema do not include this draw's base vertex. Validate
        // both ends on every record and submission, even after a cache hit.
        const int64_t minimum=int64_t(range.Minimum)+draw.VertexOffset;
        const int64_t adjustedMaximum=int64_t(range.Maximum)+draw.VertexOffset;
        if(minimum<0 || adjustedMaximum>UINT32_MAX)
            return Command->Fail("DrawIndexed vertex offset produces an invalid index.");
        maximum=static_cast<uint64_t>(adjustedMaximum);
    }
    for(const auto& binding:pipeline->Bindings) {
        const auto& source=draw.Vertices[binding.Binding];
        const uint64_t lastElement=binding.Rate==VertexInputRate::Instance?
            uint64_t(draw.FirstInstance)+draw.Instances-1:maximum;
        auto* buffer=owner->Resource<BufferSDL>(source.Resource.Pointer,ResourceKindSDL::Buffer);
        if(!buffer || buffer->_Guid!=source.Resource.ID || !buffer->Native || source.Offset>buffer->Desc.Size ||
           lastElement*binding.Stride+pipeline->AttributeEnds[binding.Binding]>buffer->Desc.Size-source.Offset)
            return Command->Fail("Draw vertex/instance span is missing, released or out of bounds.");
    }
    return true;
}
void RenderPassSDL::Draw(uint32_t count,uint32_t instances,uint32_t first,uint32_t firstInstance) {
    Command->Owner->ClearError();if(!Usable())return;
    State.Indexed=false;State.Count=count;State.Instances=instances;State.First=first;State.FirstInstance=firstInstance;State.VertexOffset=0;
    if(ValidateDraw(State))Draws.push_back(State);
}
void RenderPassSDL::DrawIndexed(uint32_t count,uint32_t instances,uint32_t first,int32_t vertexOffset,uint32_t firstInstance) {
    Command->Owner->ClearError();if(!Usable())return;
    State.Indexed=true;State.Count=count;State.Instances=instances;State.First=first;State.FirstInstance=firstInstance;State.VertexOffset=vertexOffset;
    if(ValidateDraw(State))Draws.push_back(State);
}
}
