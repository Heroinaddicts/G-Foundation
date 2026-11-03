#include "RuntimeRenderResourceCache.h"
#include "GameSproutRuntime.h"
#include <stdexcept>

namespace GameSproutRuntime {
RenderResourceLease::RenderResourceLease(Runtime& owner,GRHI::IDevice& device,IRuntimeResourceProvider& provider)
    :_Owner(owner),_Provider(&provider) {
    auto found=_Owner._RenderResourceCaches.find(_Provider);
    if(found==_Owner._RenderResourceCaches.end()) {
        auto* cache=new RenderResourceCache(device,provider);
        try {found=_Owner._RenderResourceCaches.emplace(_Provider,cache).first;}
        catch(...){delete cache;throw;}
    }
    if(&found->second->Device!=&device)throw std::logic_error("Shared render resources require the same Runtime device");
    _Cache=found->second;++_Cache->Leases;
}
RenderResourceLease::~RenderResourceLease() {
    if(!_Cache)return;
    if(--_Cache->Leases==0) {
        const auto found=_Owner._RenderResourceCaches.find(_Provider);
        if(found!=_Owner._RenderResourceCaches.end()&&found->second==_Cache)_Owner._RenderResourceCaches.erase(found);
        delete _Cache;
    }
}
void RenderResourceCache::ClearPreparedResources() {
    for(auto& entry:ShaderPipelines)delete entry.second;
    ShaderPipelines.clear();
    for(auto& entry:UserShaders){delete entry.second.Vertex;delete entry.second.Fragment;}
    UserShaders.clear();
    for(auto& entry:Meshes){delete entry.second.Buffer;delete entry.second.ShaderVertexBuffer;}
    for(auto& entry:Textures){delete entry.second.Texture;delete entry.second.Sampler;}
    Meshes.clear();Textures.clear();Materials.clear();
}
RenderResourceCache::~RenderResourceCache() {
    ClearPreparedResources();
    for(auto& entry:Pipelines)delete entry.second;
    for(auto* shader:Shaders)delete shader;
    delete White;delete DefaultSampler;delete Grid;
}
void RenderResourceCache::Synchronize() {
    const auto revision=Provider.GetRevision();
    if(_RevisionKnown&&revision==_Revision)return;
    // Only this cache releases shared values. Other viewports may still hold
    // borrowed addresses, but every Render/query drops its local snapshot when
    // it next observes Generation, before dereferencing any such address.
    ClearPreparedResources();_Revision=revision;_RevisionKnown=true;
    if(++Generation==0)++Generation;
}
}
