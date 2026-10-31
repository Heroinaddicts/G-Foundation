#include "CommandBufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include <cstring>
#include <algorithm>

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
        if(draw.VertexUniforms[slot].empty())return Command->Fail("Draw is missing a vertex uniform binding.");
    for(uint32_t slot=0;slot<pipeline->FragmentUniformCount;++slot)
        if(draw.FragmentUniforms[slot].empty())return Command->Fail("Draw is missing a fragment uniform binding.");
    uint64_t maximum=uint64_t(draw.First)+draw.Count-1;
    if(draw.Indexed) {
        auto* indices=owner->Resource<BufferSDL>(draw.Index.Resource.Pointer,ResourceKindSDL::Buffer);
        const uint64_t unit=draw.Indices==IndexFormat::UInt16?2:4;
        if(!indices || indices->_Guid!=draw.Index.Resource.ID || !indices->Native ||
           draw.Index.Offset>indices->Desc.Size || (uint64_t(draw.First)+draw.Count)*unit>indices->Desc.Size-draw.Index.Offset)
            return Command->Fail("DrawIndexed index span is missing, released or out of bounds.");
        maximum=0;
        for(uint32_t index=0;index<draw.Count;++index) {
            const size_t offset=draw.Index.Offset+(uint64_t(draw.First)+index)*unit;
            uint32_t vertex=0;
            if(unit==2){uint16_t value;std::memcpy(&value,indices->IndexBytes.data()+offset,2);vertex=value;}
            else std::memcpy(&vertex,indices->IndexBytes.data()+offset,4);
            const int64_t adjusted=int64_t(vertex)+draw.VertexOffset;
            if(adjusted<0 || adjusted>UINT32_MAX)return Command->Fail("DrawIndexed vertex offset produces an invalid index.");
            maximum=std::max(maximum,static_cast<uint64_t>(adjusted));
        }
    }
    for(const auto& binding:pipeline->Bindings) {
        const auto& source=draw.Vertices[binding.Binding];
        auto* buffer=owner->Resource<BufferSDL>(source.Resource.Pointer,ResourceKindSDL::Buffer);
        if(!buffer || buffer->_Guid!=source.Resource.ID || !buffer->Native || source.Offset>buffer->Desc.Size ||
           maximum*binding.Stride+pipeline->AttributeEnds[binding.Binding]>buffer->Desc.Size-source.Offset)
            return Command->Fail("Draw vertex span is missing, released or out of bounds.");
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
