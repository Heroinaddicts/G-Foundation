#include "CommandBufferSDL.h"
#include "Resource/PipelineSDL.h"
#include "Resource/BufferSDL.h"
#include "Resource/TextureSDL.h"
#include "Resource/SamplerSDL.h"
#include <bit>

namespace GRHI {
namespace {
constexpr uint32_t SlotMask(uint32_t count) { return (1u<<count)-1; }

template<typename Binding,typename Resource>
void ForBoundSlots(uint32_t mask,const Binding& bindings,Resource resource) {
    while(mask) {
        const auto slot=std::countr_zero(mask);
        resource(bindings[slot]);
        mask&=mask-1;
    }
}
}

bool RenderPassSDL::RefreshBindings() {
    const auto revision=Command->Owner->ResourceReleaseRevision();
    if(revision==_ResourceRevision)return true;
    const auto live=[this](const ResourceBindingSDL& binding) {
        return Command->Owner->IsResourceLive(binding.Pointer,binding.ID);
    };
    bool valid=true;
    ForBoundSlots(_VertexBound,State.Vertices,[&](const auto& buffer){valid=valid&&live(buffer.Resource);});
    ForBoundSlots(_SamplerBound,State.Textures,[&](const auto& texture){valid=valid&&live(texture);});
    ForBoundSlots(_SamplerBound,State.Samplers,[&](const auto& sampler){valid=valid&&live(sampler);});
    ForBoundSlots(_VertexStorageBound,State.VertexStorage,[&](const auto& storage){valid=valid&&live(storage);});
    if(!valid)
        return Command->Fail("Draw uses a released buffer, texture or sampler.");
    if(_IndexBound&&!live(State.Index.Resource))_IndexBound=false;
    _ResourceRevision=revision;
    return true;
}

bool RenderPassSDL::ValidateResources() const {
    for(const auto& binding:_RecordedResources)
        if(!Command->Owner->IsResourceLive(binding.Pointer,binding.ID))
            return Command->Fail("A resource used by a recorded draw was released before submission.");
    return true;
}

bool RenderPassSDL::ValidateDraw(const NativeDrawSDL& draw,const PipelineSDL& pipeline) {
    if(!draw.Count||!draw.Instances||draw.First>UINT32_MAX-draw.Count||
       draw.FirstInstance>UINT32_MAX-draw.Instances)
        return Command->Fail("Draw requires live pipeline and nonoverflowing vertex/index/instance ranges.");
    if(_SamplerBound!=SlotMask(pipeline.SamplerCount))
        return Command->Fail("Draw is missing a texture or sampler.");
    if(_VertexUniformBound!=SlotMask(pipeline.VertexUniformCount))
        return Command->Fail("Draw is missing a vertex uniform binding.");
    if(_VertexStorageBound!=SlotMask(pipeline.VertexStorageCount))
        return Command->Fail("Draw is missing a vertex storage buffer.");
    if(_FragmentUniformBound!=SlotMask(pipeline.FragmentUniformCount))
        return Command->Fail("Draw is missing a fragment uniform binding.");
    if(_VertexBound!=pipeline.BindingMask)
        return Command->Fail("Draw is missing a vertex or instance buffer binding.");
    if(!RefreshBindings())return false;

    uint64_t maximum=uint64_t(draw.First)+draw.Count-1;
    if(draw.Indexed) {
        if(!_IndexBound)return Command->Fail("DrawIndexed is missing an index buffer.");
        auto* indices=static_cast<BufferSDL*>(State.Index.Resource.Implementation);
        const uint64_t unit=State.Indices==IndexFormat::UInt16?2:4;
        if((uint64_t(draw.First)+draw.Count)*unit>indices->Desc.Size-State.Index.Offset)
            return Command->Fail("DrawIndexed index span is out of bounds.");
        const auto byteOffset=State.Index.Offset+uint64_t(draw.First)*unit;
        if(indices!=_LastIndexBuffer||State.Index.Resource.ID!=_LastIndexID||byteOffset!=_LastIndexByteOffset||
           draw.Count!=_LastIndexCount||State.Indices!=_LastIndexFormat) {
            IndexRangeCacheSDL::Range range;
            if(!indices->GetIndexRange(byteOffset,draw.Count,State.Indices,range))
                return Command->Fail("DrawIndexed index format, alignment or immutable byte range is invalid.");
            _LastIndexBuffer=indices;
            _LastIndexID=State.Index.Resource.ID;
            _LastIndexByteOffset=byteOffset;
            _LastIndexCount=draw.Count;
            _LastIndexFormat=State.Indices;
            _LastIndexMinimum=range.Minimum;
            _LastIndexMaximum=range.Maximum;
        }
        // Immutable bounds are reusable; base vertex remains draw-specific.
        const int64_t minimum=int64_t(_LastIndexMinimum)+draw.VertexOffset;
        const int64_t adjustedMaximum=int64_t(_LastIndexMaximum)+draw.VertexOffset;
        if(minimum<0||adjustedMaximum>UINT32_MAX)
            return Command->Fail("DrawIndexed vertex offset produces an invalid index.");
        maximum=static_cast<uint64_t>(adjustedMaximum);
    }
    for(const auto& binding:pipeline.Bindings) {
        const auto& source=State.Vertices[binding.Binding];
        const auto* buffer=static_cast<const BufferSDL*>(source.Resource.Implementation);
        const uint64_t lastElement=binding.Rate==VertexInputRate::Instance?
            uint64_t(draw.FirstInstance)+draw.Instances-1:maximum;
        if(lastElement*binding.Stride+pipeline.AttributeEnds[binding.Binding]>buffer->Desc.Size-source.Offset)
            return Command->Fail("Draw vertex or instance span is out of bounds.");
    }
    return true;
}

void RenderPassSDL::RecordDraw(const NativeDrawSDL& draw) {
    Command->Owner->ClearError();
    if(!Usable())return;
    const auto* pipeline=CurrentPipeline();
    if(!pipeline) {
        Command->Fail("Draw requires a live pipeline.");
        return;
    }
    if(!ValidateDraw(draw,*pipeline))return;
    if(_Replay.Pipeline(State.Pipeline)) {
        _RecordedResources.push_back(State.Pipeline);
        _Operations.emplace_back(BindPipelineSDL{pipeline->Native});
    }
    const auto& view=State.View;
    if(_Replay.View(view))
        _Operations.emplace_back(BindViewportSDL{{view.X,view.Y,view.Width,view.Height,view.MinDepth,view.MaxDepth}});
    for(const auto& binding:pipeline->Bindings) {
        const auto& source=State.Vertices[binding.Binding];
        if(!_Replay.Vertex(binding.Binding,source))continue;
        const auto* buffer=static_cast<const BufferSDL*>(source.Resource.Implementation);
        _RecordedResources.push_back(source.Resource);
        _Operations.emplace_back(BindVertexSDL{binding.Binding,{buffer->Native,static_cast<uint32_t>(source.Offset)}});
    }
    for(uint32_t slot=0;slot<pipeline->VertexStorageCount;++slot) {
        const auto& source=State.VertexStorage[slot];
        if(!_Replay.VertexStorage(slot,source))continue;
        const auto* buffer=static_cast<const BufferSDL*>(source.Implementation);
        _RecordedResources.push_back(source);
        _Operations.emplace_back(BindVertexStorageSDL{slot,buffer->Native});
    }
    for(uint32_t slot=0;slot<pipeline->VertexUniformCount;++slot) {
        const auto* bytes=State.VertexUniforms[slot];
        if(_Replay.Uniform(true,slot,bytes))_Operations.emplace_back(BindUniformSDL{true,slot,bytes});
    }
    for(uint32_t slot=0;slot<pipeline->FragmentUniformCount;++slot) {
        const auto* bytes=State.FragmentUniforms[slot];
        if(_Replay.Uniform(false,slot,bytes))_Operations.emplace_back(BindUniformSDL{false,slot,bytes});
    }
    if(pipeline->SamplerCount&&_Replay.Samplers(State,pipeline->SamplerCount)) {
        const auto first=_NativeSamplers.size();
        for(uint32_t slot=0;slot<pipeline->SamplerCount;++slot) {
            const auto& texture=State.Textures[slot];
            const auto& sampler=State.Samplers[slot];
            _RecordedResources.push_back(texture);
            _RecordedResources.push_back(sampler);
            _NativeSamplers.push_back({static_cast<const TextureSDL*>(texture.Implementation)->Native,
                static_cast<const SamplerSDL*>(sampler.Implementation)->Native});
        }
        _Operations.emplace_back(BindSamplersSDL{first,pipeline->SamplerCount});
    }
    if(draw.Indexed&&_Replay.Index(State.Index,State.Indices)) {
        const auto& source=State.Index;
        const auto* buffer=static_cast<const BufferSDL*>(source.Resource.Implementation);
        _RecordedResources.push_back(source.Resource);
        const auto format=State.Indices==IndexFormat::UInt16?SDL_GPU_INDEXELEMENTSIZE_16BIT:SDL_GPU_INDEXELEMENTSIZE_32BIT;
        _Operations.emplace_back(BindIndexSDL{{buffer->Native,static_cast<uint32_t>(source.Offset)},format});
    }
    _Operations.emplace_back(draw);
}

void RenderPassSDL::Draw(uint32_t count,uint32_t instances,uint32_t first,uint32_t firstInstance) {
    RecordDraw({false,count,instances,first,firstInstance,0});
}
void RenderPassSDL::DrawIndexed(uint32_t count,uint32_t instances,uint32_t first,int32_t vertexOffset,uint32_t firstInstance) {
    RecordDraw({true,count,instances,first,firstInstance,vertexOffset});
}
}
