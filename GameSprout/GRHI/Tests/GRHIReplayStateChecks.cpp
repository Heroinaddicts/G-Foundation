#include "ReplayStateSDL.h"
#include <array>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const char* error) {if(!value)throw std::runtime_error(error);}
int main() {
    try {
        GRHI::UniformArenaSDL arena;
        std::array<float,4> material{1,.5f,.25f,1};
        const auto* original=arena.Store(material.data(),sizeof(material));
        const auto expected=*original;
        Check(arena.Store(material.data(),sizeof(material))==original && arena.Size()==1,"Identical uniform snapshots were copied");
        material[0]=0;
        const auto* changed=arena.Store(material.data(),sizeof(material));
        Check(changed!=original && *original==expected,"Caller mutation overwrote recorded uniform bytes");
        for(unsigned value=0;value<512;++value) {
            const std::array<float,4> varying{float(value),.2f,.3f,1};
            arena.Store(varying.data(),sizeof(varying));
        }
        Check(*original==expected && arena.Store(expected.data(),expected.size())==original,"Uniform arena growth invalidated snapshots or deduplication");
        int pipeline1,pipeline2,vertex,index,texture,sampler;
        GRHI::DrawRecordSDL draw;
        draw.Pipeline={&pipeline1,1};draw.Vertices[0]={{&vertex,2},0};draw.Index={{&index,3},12};
        draw.Textures[0]={&texture,4};draw.Samplers[0]={&sampler,5};draw.VertexUniforms[0]=original;draw.FragmentUniforms[0]=changed;
        draw.View.Width=64;draw.View.Height=64;
        GRHI::ReplayStateSDL state;
        std::size_t pipelineBinds=0,viewBinds=0,vertexBinds=0,indexBinds=0,uniformPushes=0,samplerBinds=0;
        for(unsigned repeat=0;repeat<1000;++repeat) {
            pipelineBinds+=state.Pipeline(draw.Pipeline);viewBinds+=state.View(draw.View);
            vertexBinds+=state.Vertex(0,draw.Vertices[0]);indexBinds+=state.Index(draw.Index,draw.Indices);
            uniformPushes+=state.Uniform(true,0,draw.VertexUniforms[0]);uniformPushes+=state.Uniform(false,0,draw.FragmentUniforms[0]);
            samplerBinds+=state.Samplers(draw,1);
        }
        Check(pipelineBinds==1 && viewBinds==1 && vertexBinds==1 && indexBinds==1 && uniformPushes==2 && samplerBinds==1,
            "Repeated draws did not eliminate redundant native state changes");
        auto equalBytes=*original;
        Check(!state.Uniform(true,0,&equalBytes),"Uniform reuse depends on pointer identity rather than exact bytes");
        Check(state.Uniform(true,0,changed),"Changed uniform bytes were not pushed");
        auto offset=draw.Vertices[0];offset.Offset=4;
        Check(state.Vertex(0,offset),"Vertex byte offset change was ignored");
        Check(state.Index(draw.Index,GRHI::IndexFormat::UInt16),"Index format change was ignored");
        draw.Textures[0].ID=99;
        Check(state.Samplers(draw,1),"Resource identity change was ignored");
        draw.Pipeline={&pipeline2,6};
        Check(state.Pipeline(draw.Pipeline),"Pipeline change was ignored");
        Check(state.Vertex(0,draw.Vertices[0]) && state.Index(draw.Index,draw.Indices) &&
            state.Uniform(true,0,original) && state.Uniform(false,0,changed) && state.Samplers(draw,1),
            "Pipeline change failed to invalidate shader/geometry state");
        Check(!state.View(draw.View),"Independent viewport was unnecessarily invalidated by a pipeline change");
        GRHI::ReplayStateSDL nextPass;
        Check(nextPass.Pipeline(draw.Pipeline) && nextPass.View(draw.View) && nextPass.Uniform(true,0,original),
            "State leaked across render passes");
        std::cout<<"PASS: 1000 draws share uniform snapshots and emit one compatible pipeline/viewport/buffer/sampler binding; exact changes and pipeline/pass boundaries invalidate correctly\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
