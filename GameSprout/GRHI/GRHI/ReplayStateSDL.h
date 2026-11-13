#pragma once
#include "GRHIApi.h"
#include <array>

namespace GRHI {
struct ResourceBindingSDL {
    const void* Pointer = nullptr;
    uint64_t ID = 0;
    void* Implementation = nullptr;
};
struct BufferBindingSDL { ResourceBindingSDL Resource; size_t Offset = 0; };
struct PassBindingsSDL {
    ResourceBindingSDL Pipeline;
    std::array<BufferBindingSDL,16> Vertices{};
    BufferBindingSDL Index;
    IndexFormat Indices = IndexFormat::UInt32;
    std::array<ResourceBindingSDL,16> Textures{},Samplers{};
    std::array<ResourceBindingSDL,8> VertexStorage{};
    // Borrowed immutable bytes owned by this pass, not by a caller or a draw.
    std::array<const std::vector<uint8_t>*,4> VertexUniforms{},FragmentUniforms{};
    Viewport View;
};
// Native bindings are remembered only inside one encoded render pass. A pipeline
// change conservatively invalidates shader/geometry bindings for every backend;
// viewport is independent. This state accumulates validated deltas at record time.
class ReplayStateSDL {
public:
    bool Pipeline(const ResourceBindingSDL& value) {
        if(Same(_Pipeline,value))return false;
        _Pipeline=value;
        _Vertices={};_Index={};_Textures={};_Samplers={};_VertexStorage={};
        _VertexUniforms={};_FragmentUniforms={};_SamplerCount=0;
        return true;
    }
    bool View(const Viewport& value) {
        if(_HasView && _View.X==value.X && _View.Y==value.Y && _View.Width==value.Width && _View.Height==value.Height &&
           _View.MinDepth==value.MinDepth && _View.MaxDepth==value.MaxDepth)return false;
        _View=value;_HasView=true;return true;
    }
    bool Vertex(std::uint32_t slot,const BufferBindingSDL& value) {
        if(Same(_Vertices[slot],value))return false;
        _Vertices[slot]=value;return true;
    }
    bool VertexStorage(std::uint32_t slot,const ResourceBindingSDL& value) {
        if(Same(_VertexStorage[slot],value))return false;
        _VertexStorage[slot]=value;return true;
    }
    bool Index(const BufferBindingSDL& value,IndexFormat format) {
        if(Same(_Index,value) && _Indices==format)return false;
        _Index=value;_Indices=format;return true;
    }
    bool Uniform(bool vertex,std::uint32_t slot,const std::vector<std::uint8_t>* value) {
        auto& previous=vertex?_VertexUniforms[slot]:_FragmentUniforms[slot];
        if(previous==value || (previous && value && *previous==*value))return false;
        previous=value;return true;
    }
    bool Samplers(const PassBindingsSDL& value,std::uint32_t count) {
        bool changed=_SamplerCount!=count;
        for(std::uint32_t slot=0;slot<count;++slot) {
            changed=changed || !Same(_Textures[slot],value.Textures[slot]) || !Same(_Samplers[slot],value.Samplers[slot]);
            _Textures[slot]=value.Textures[slot];_Samplers[slot]=value.Samplers[slot];
        }
        _SamplerCount=count;return changed;
    }
private:
    static bool Same(const ResourceBindingSDL& a,const ResourceBindingSDL& b) {return a.Pointer==b.Pointer && a.ID==b.ID;}
    static bool Same(const BufferBindingSDL& a,const BufferBindingSDL& b) {return Same(a.Resource,b.Resource) && a.Offset==b.Offset;}
    ResourceBindingSDL _Pipeline;
    std::array<BufferBindingSDL,16> _Vertices{};
    BufferBindingSDL _Index;
    IndexFormat _Indices=IndexFormat::UInt32;
    std::array<ResourceBindingSDL,16> _Textures{},_Samplers{};
    std::array<ResourceBindingSDL,8> _VertexStorage{};
    std::array<const std::vector<std::uint8_t>*,4> _VertexUniforms{},_FragmentUniforms{};
    std::uint32_t _SamplerCount=0;
    Viewport _View;
    bool _HasView=false;
};
}
