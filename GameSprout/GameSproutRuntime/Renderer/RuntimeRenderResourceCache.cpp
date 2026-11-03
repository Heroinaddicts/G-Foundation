#include "RuntimeRenderResourceCache.h"
#include "GameSproutRuntime.h"
#include "Api/RuntimeBuiltinResources.h"
#include <set>
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
    for(auto& entry:SkyPipelines)delete entry.second;
    for(auto* shader:Shaders)delete shader;
    delete White;delete DefaultSampler;delete Grid;
}
void RenderResourceCache::Synchronize() {
    const auto revision=Provider.GetRevision();
    if(_RevisionKnown&&revision==_Revision)return;
    std::set<Renderer::ResourceKey> meshes,textures,materials,shaders;
    const auto changed=[&](const auto& values,auto& output) {
        for(const auto& [key,value]:values) {
            const ResourceReference reference{key.Guid,key.LocalID};
            if(!IsBuiltinResource(reference)&&Provider.GetResourceRevision(reference)!=value.ResourceRevision)
                output.insert(key);
        }
    };
    // A source-status event may change the provider revision without changing
    // any artifact. Compare only resident resources, including failed loads, by
    // their complete identity; a sibling LocalID is never an invalidation key.
    changed(Meshes,meshes);changed(Textures,textures);
    changed(Materials,materials);changed(UserShaders,shaders);
    _Revision=revision;_RevisionKnown=true;
    if(meshes.empty()&&textures.empty()&&materials.empty()&&shaders.empty())return;

    // Material bindings borrow TextureGPU addresses and depend on the Shader's
    // uniform layout. Reset those bindings before releasing either dependency,
    // while retaining unchanged decoded material data and its resource token.
    for(auto& [key,material]:Materials) {
        if(textures.count(Renderer::ResourceKey(material.Data.BaseColorTexture))) {
            material.BaseColorTexture=nullptr;material.BaseColorAttempted=false;material.BaseColorError.clear();
        }
        if(textures.count(Renderer::ResourceKey(material.Data.NormalTexture))) {
            material.NormalTexture=nullptr;material.NormalAttempted=false;material.NormalError.clear();
        }
        bool reset=shaders.count(Renderer::ResourceKey(material.Data.Shader))!=0;
        for(const auto& binding:material.Data.Textures)
            reset|=textures.count(Renderer::ResourceKey(binding.Texture))!=0;
        if(reset) {
            material.ShaderAttempted=false;material.ShaderReady=false;
            material.VertexUniforms.clear();material.FragmentUniforms.clear();material.ShaderTextures.clear();
            if(material.Loaded)material.Error.clear();
        }
    }
    for(auto pipeline=ShaderPipelines.begin();pipeline!=ShaderPipelines.end();) {
        const Renderer::ResourceKey key({pipeline->first.Guid,pipeline->first.LocalID});
        if(shaders.count(key)){delete pipeline->second;pipeline=ShaderPipelines.erase(pipeline);}
        else ++pipeline;
    }
    for(const auto& key:shaders) {
        const auto entry=UserShaders.find(key);
        delete entry->second.Vertex;delete entry->second.Fragment;UserShaders.erase(entry);
    }
    for(const auto& key:textures) {
        const auto entry=Textures.find(key);
        delete entry->second.Texture;delete entry->second.Sampler;Textures.erase(entry);
    }
    for(const auto& key:meshes) {
        const auto entry=Meshes.find(key);
        delete entry->second.Buffer;delete entry->second.ShaderVertexBuffer;Meshes.erase(entry);
    }
    for(const auto& key:materials)Materials.erase(key);
    // Only this cache releases shared values. Other viewports may still hold
    // borrowed addresses, but every Render/query drops its local snapshot when
    // it next observes Generation, before dereferencing any such address.
    if(++Generation==0)++Generation;
}
}
