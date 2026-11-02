#include "RuntimeRenderer.h"
#include <algorithm>
#include <cstring>

namespace GameSproutRuntime {
void Renderer::SortPrepared(std::vector<PreparedDraw>& draws) {
    // Queue class always wins over material priority. Transparent draws remain
    // back-to-front inside each priority; custom vertex/depth programs are not
    // assumed order independent and remain barriers to opaque state sorting.
    std::stable_sort(draws.begin(),draws.end(),[](const PreparedDraw& left,const PreparedDraw& right) {
        if(left.Transparent!=right.Transparent)return !left.Transparent;
        if(left.Material->SortingPriority!=right.Material->SortingPriority)
            return left.Material->SortingPriority<right.Material->SortingPriority;
        return left.Transparent&&left.DistanceSquared>right.DistanceSquared;
    });
    for(std::size_t begin=0;begin<draws.size();) {
        if(draws[begin].Transparent||draws[begin].Shader){++begin;continue;}
        std::size_t end=begin+1;
        while(end<draws.size()&&!draws[end].Transparent&&!draws[end].Shader&&
            draws[end].Material->SortingPriority==draws[begin].Material->SortingPriority)++end;
        std::stable_sort(draws.begin()+begin,draws.begin()+end,[](const PreparedDraw& left,const PreparedDraw& right) {
            return std::tie(left.Material->ShadingModel,left.Material->DoubleSided,left.MaterialReference.Guid,left.MaterialReference.LocalID,
                left.Object->Mesh.Guid,left.Object->Mesh.LocalID,left.Section.FirstIndex,left.Section.IndexCount,
                left.Object->Negative,left.Object->Layer,left.Object->Skinned)<
                std::tie(right.Material->ShadingModel,right.Material->DoubleSided,right.MaterialReference.Guid,right.MaterialReference.LocalID,
                right.Object->Mesh.Guid,right.Object->Mesh.LocalID,right.Section.FirstIndex,right.Section.IndexCount,
                right.Object->Negative,right.Object->Layer,right.Object->Skinned);
        });
        begin=end;
    }
}

void Renderer::ClearInstanceBuffers() {
    for(auto& entry:_InstanceBatches)delete entry.second.Buffer;
    _InstanceBatches.clear();_InstanceScratch.clear();_ResidentInstanceBytes=0;
}

void Renderer::PruneInstanceBuffers(std::uint64_t requiredBytes) {
    // Visibility and scene switches can leave old groups behind. Keep a short
    // reuse window, and a hard byte budget even while continuously moving the
    // camera. Never release a buffer already referenced by this frame's command.
    if(!requiredBytes)for(auto item=_InstanceBatches.begin();item!=_InstanceBatches.end();) {
        if(_Frame-item->second.LastUsed>2) {
            _ResidentInstanceBytes-=item->second.Vertices.size()*sizeof(InstanceVertex);
            delete item->second.Buffer;item=_InstanceBatches.erase(item);
        }else ++item;
    }
    while(_ResidentInstanceBytes+requiredBytes>MaxResidentInstanceBytes) {
        auto oldest=_InstanceBatches.end();
        for(auto item=_InstanceBatches.begin();item!=_InstanceBatches.end();++item)
            if(item->second.LastUsed!=_Frame&&(oldest==_InstanceBatches.end()||item->second.LastUsed<oldest->second.LastUsed))oldest=item;
        if(oldest==_InstanceBatches.end())break;
        _ResidentInstanceBytes-=oldest->second.Vertices.size()*sizeof(InstanceVertex);
        delete oldest->second.Buffer;_InstanceBatches.erase(oldest);
    }
}

GRHI::IBuffer* Renderer::InstanceBuffer(const InstanceBatchKey& key,const std::vector<PreparedDraw>& draws,
    std::size_t begin,std::size_t count) {
    _InstanceScratch.clear();_InstanceScratch.reserve(count);
    for(std::size_t index=begin;index<begin+count;++index)
        _InstanceScratch.push_back({draws[index].Object->Model,draws[index].Object->Normal});
    const auto bytes=count*sizeof(InstanceVertex);
    auto cached=_InstanceBatches.find(key);
    if(cached!=_InstanceBatches.end()) {
        auto& batch=cached->second;
        if(batch.Vertices.size()==count&&!std::memcmp(batch.Vertices.data(),_InstanceScratch.data(),bytes)) {
            batch.LastUsed=_Frame;return batch.Buffer;
        }
        // Never replace a resource referenced by a previously recorded draw.
        // GroupOrdinal is a source-section ordinal, stable when unrelated
        // objects enter/leave the view. It distinguishes repeated sections separated by
        // custom-shader order barriers, even on the same GameObject.
        if(batch.LastUsed==_Frame)return nullptr;
        _ResidentInstanceBytes-=batch.Vertices.size()*sizeof(InstanceVertex);
        delete batch.Buffer;_InstanceBatches.erase(cached);
    }
    PruneInstanceBuffers(bytes);
    if(_ResidentInstanceBytes+bytes>MaxResidentInstanceBytes)return nullptr;
    GRHI::BufferDesc description;description.Size=bytes;description.Usage=GRHI::BufferUsage::Vertex;
    const GRHI::BufferInitialData initial{_InstanceScratch.data(),bytes};
    auto* buffer=_Device.CreateBuffer(description,&initial);
    if(!buffer){Diagnostic(DeviceError("Instance buffer upload failed; drawing individual objects"));return nullptr;}
    try {
        auto& batch=_InstanceBatches[key];
        batch.Vertices=_InstanceScratch;batch.LastUsed=_Frame;batch.Buffer=buffer;
    }catch(...){delete buffer;_InstanceBatches.erase(key);throw;}
    _ResidentInstanceBytes+=bytes;++_Statistics.InstanceBufferUploads;
    return buffer;
}

bool Renderer::DrawPrepared(GRHI::IRenderPass& pass,const std::vector<PreparedDraw>& draws,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format,const std::vector<DirectionalLight>& lights,
    const void* environment,std::size_t environmentSize) {
    for(std::size_t begin=0;begin<draws.size();) {
        const auto& draw=draws[begin];
        if(draw.Shader) {
            if(!DrawShaderMaterial(pass,*draw.CachedMaterial,*draw.Shader,*draw.Mesh,draw.Material->Shader,
                draw.Section,*draw.Object,viewProjection,format))return false;
            ++_Statistics.DrawCalls;_Statistics.Triangles+=draw.Section.IndexCount/3;++begin;continue;
        }
        const bool eligible=view.EnableGPUInstancing&&draw.Material->EnableGPUInstancing&&!draw.Transparent&&!draw.Object->Skinned;
        std::size_t end=begin+1;
        if(eligible)while(end<draws.size()) {
            const auto& next=draws[end];
            // Material identity also covers textures, uniforms, priority, shader
            // and cull mode. Layer splits per-object light selection. Mirrored
            // transforms require a separate front-face pipeline.
            if(next.Shader||next.Transparent||next.Object->Skinned||next.Mesh!=draw.Mesh||next.Material!=draw.Material||
                next.Section.FirstIndex!=draw.Section.FirstIndex||next.Section.IndexCount!=draw.Section.IndexCount||
                next.Object->Negative!=draw.Object->Negative||next.Object->Layer!=draw.Object->Layer)break;
            ++end;
        }
        for(std::uint32_t part=0;begin<end;++part) {
            const auto count=std::min(MaxBatchInstances,end-begin);
            GRHI::IBuffer* instances=nullptr;
            if(count>1) {
                InstanceBatchKey key;key.SceneID=view.Scene->GetInstanceID();key.FirstObjectID=draws[begin].Object->ObjectID;key.Mesh=draw.Object->Mesh;
                key.GroupOrdinal=draws[begin].SectionOrdinal;
                key.Material=draw.MaterialReference;key.FirstIndex=draw.Section.FirstIndex;key.IndexCount=draw.Section.IndexCount;
                key.Part=part;key.Layer=draw.Object->Layer;key.Negative=draw.Object->Negative;
                instances=InstanceBuffer(key,draws,begin,count);
            }
            if(instances) {
                if(!DrawBuiltin(pass,draws[begin],view,viewProjection,format,lights,environment,environmentSize,instances,static_cast<std::uint32_t>(count)))return false;
                ++_Statistics.InstancedDrawCalls;_Statistics.InstancedInstances+=static_cast<std::uint32_t>(count);
                _Statistics.SavedDrawCalls+=static_cast<std::uint32_t>(count-1);
            }else for(std::size_t index=begin;index<begin+count;++index)
                if(!DrawBuiltin(pass,draws[index],view,viewProjection,format,lights,environment,environmentSize))return false;
            begin+=count;
        }
    }
    _Statistics.ResidentInstanceBytes=_ResidentInstanceBytes;
    return true;
}
}
