#include "RuntimeRenderer.h"
#include <algorithm>
#include <limits>

namespace GameSproutRuntime {
void Renderer::RefreshDrawSortRanks() {
    // Assign numeric ranks in the same GUID/LocalID order as the resource maps.
    // The order stays identical to the previous string comparator, but static
    // frames no longer compare or copy GUID strings for every visible section.
    if(_SortedMeshCount!=_Meshes.size()) {
        std::uint64_t rank=0;for(auto& entry:_Meshes)entry.second.SortRank=++rank;
        _SortedMeshCount=_Meshes.size();
    }
    if(_SortedMaterialCount!=_Materials.size()) {
        std::uint64_t rank=0;for(auto& entry:_Materials)entry.second.SortRank=++rank;
        _SortedMaterialCount=_Materials.size();
    }
}

bool Renderer::PriorityAndDepthBefore(const PreparedDraw& left,const PreparedDraw& right) {
    if(left.Transparent!=right.Transparent)return !left.Transparent;
    if(left.Material->SortingPriority!=right.Material->SortingPriority)
        return left.Material->SortingPriority<right.Material->SortingPriority;
    return left.Transparent&&left.DistanceSquared>right.DistanceSquared;
}

bool Renderer::OpaqueDrawBefore(const PreparedDraw& left,const PreparedDraw& right) {
    if(left.Material->ShadingModel!=right.Material->ShadingModel)return left.Material->ShadingModel<right.Material->ShadingModel;
    if(left.Material->DoubleSided!=right.Material->DoubleSided)return left.Material->DoubleSided<right.Material->DoubleSided;
    const auto leftMaterial=left.CachedMaterial?left.CachedMaterial->SortRank:0;
    const auto rightMaterial=right.CachedMaterial?right.CachedMaterial->SortRank:0;
    if(leftMaterial!=rightMaterial)return leftMaterial<rightMaterial;
    if(left.Mesh->SortRank!=right.Mesh->SortRank)return left.Mesh->SortRank<right.Mesh->SortRank;
    if(left.Section.FirstIndex!=right.Section.FirstIndex)return left.Section.FirstIndex<right.Section.FirstIndex;
    if(left.Section.IndexCount!=right.Section.IndexCount)return left.Section.IndexCount<right.Section.IndexCount;
    if(left.Object->Negative!=right.Object->Negative)return left.Object->Negative<right.Object->Negative;
    if(left.Object->Layer!=right.Object->Layer)return left.Object->Layer<right.Object->Layer;
    if(left.Material->ShadingModel==MaterialShadingModel::Lit&&left.Object->ReceiveShadows!=right.Object->ReceiveShadows)
        return left.Object->ReceiveShadows<right.Object->ReceiveShadows;
    return left.Object->Skinned<right.Object->Skinned;
}

void Renderer::SortPrepared(std::vector<PreparedDraw>& draws) {
    // Queue class always wins over material priority. Transparent draws remain
    // back-to-front inside each priority; custom vertex/depth programs are not
    // assumed order independent and remain barriers to opaque state sorting.
    std::stable_sort(draws.begin(),draws.end(),PriorityAndDepthBefore);
    for(std::size_t begin=0;begin<draws.size();) {
        if(draws[begin].Transparent||draws[begin].Shader){++begin;continue;}
        std::size_t end=begin+1;
        while(end<draws.size()&&!draws[end].Transparent&&!draws[end].Shader&&
            draws[end].Material->SortingPriority==draws[begin].Material->SortingPriority)++end;
        std::stable_sort(draws.begin()+begin,draws.begin()+end,OpaqueDrawBefore);
        begin=end;
    }
}

namespace {
// SDL buffers take a 32-bit size. Capacity grows with scene data and never
// triggers eviction based on camera visibility or elapsed frames.
bool ReserveInstanceBuffer(GRHI::IDevice& device,GRHI::IBuffer*& buffer,
    std::size_t bytes,GRHI::BufferUsage usage) {
    if(buffer&&buffer->GetSize()>=bytes)return true;
    constexpr std::uint64_t maximum=std::numeric_limits<std::uint32_t>::max();
    if(bytes>maximum)return false;
    const std::uint64_t oldSize=buffer?buffer->GetSize():0;
    GRHI::BufferDesc description;
    description.Size=std::max<std::uint64_t>(bytes,std::min(maximum,oldSize+oldSize/2));
    description.Usage=usage;
    description.InitializeToZero=false;
    auto* replacement=device.CreateBuffer(description);
    if(!replacement)return false;
    delete buffer;
    buffer=replacement;
    return true;
}
}

void Renderer::InvalidateInstanceUploads() {
    _InstanceTransformsDirty=true;
    _ColorInstances.Uploaded.clear();
    _StaticShadowInstances.Uploaded.clear();
    _DynamicShadowInstances.Uploaded.clear();
}

void Renderer::ClearInstanceBuffers() {
    delete _InstanceTransformsBuffer;
    _InstanceTransformsBuffer=nullptr;
    _InstanceTransforms.clear();
    _ChangedInstanceTransforms.clear();
    _ColorInstanceDraws.clear();
    _ColorInstanceBatchesKnown=false;
    for(auto* stream:{&_ColorInstances,&_StaticShadowInstances,&_DynamicShadowInstances}) {
        delete stream->Buffer;
        *stream={};
    }
    InvalidateInstanceUploads();
}

void Renderer::PrepareInstanceBatches(const std::vector<PreparedDraw>& draws,bool enabled,bool shadow,
    std::vector<InstanceDraw>& batches,std::vector<std::uint32_t>& indices) const {
    batches.clear();
    for(std::size_t begin=0;begin<draws.size();) {
        const auto& draw=draws[begin];
        const bool eligible=enabled&&draw.Material->EnableGPUInstancing&&!draw.Transparent&&!draw.Shader&&!draw.Object->Skinned;
        std::size_t end=begin+1;
        if(eligible)while(end<draws.size()) {
            const auto& next=draws[end];
            if(next.Shader||next.Transparent||next.Object->Skinned||!next.Material->EnableGPUInstancing||
                next.Mesh!=draw.Mesh||next.Section.FirstIndex!=draw.Section.FirstIndex||
                next.Section.IndexCount!=draw.Section.IndexCount||next.Object->Negative!=draw.Object->Negative)break;
            if(shadow) {
                if(next.ShadowDoubleSided!=draw.ShadowDoubleSided)break;
            }else if(next.Material!=draw.Material||next.Object->Layer!=draw.Object->Layer||
                (draw.Material->ShadingModel==MaterialShadingModel::Lit&&next.Object->ReceiveShadows!=draw.Object->ReceiveShadows))break;
            ++end;
        }
        const auto count=static_cast<std::uint32_t>(end-begin);
        batches.push_back({begin,count,indices.size()});
        if(count>1)for(auto index=begin;index<end;++index)
            indices.push_back(draws[index].Object->InstanceIndex);
        begin=end;
    }
}

bool Renderer::UploadResidentInstances(GRHI::ICommandBuffer& command) {
    if(_SceneObjects.empty())return true;
    const auto requiredBytes=_SceneObjects.size()*sizeof(InstanceVertex);
    if(_InstanceTransformsDirty) {
        _InstanceTransforms.resize(_SceneObjects.size());
        for(const auto& object:_SceneObjects)
            _InstanceTransforms[object.InstanceIndex]={object.Model,object.Normal};
        if(!ReserveInstanceBuffer(_Device,_InstanceTransformsBuffer,requiredBytes,GRHI::BufferUsage::VertexStorage))
            return Fail(DeviceError("Allocate resident instance transform buffer"));
        const GRHI::BufferUpload upload{_InstanceTransformsBuffer,0,_InstanceTransforms.data(),requiredBytes,true};
        if(!command.UploadBuffers(&upload,1))return Fail(DeviceError("Upload resident instance transforms"));
        ++_Statistics.InstanceBufferUploads;
        _Statistics.InstanceTransformUploadBytes=requiredBytes;
    }else if(!_ChangedInstanceTransforms.empty()) {
        auto& changes=_ChangedInstanceTransforms;
        std::sort(changes.begin(),changes.end());
        changes.erase(std::unique(changes.begin(),changes.end()),changes.end());
        std::vector<GRHI::BufferUpload> uploads;
        for(std::size_t start=0;start<changes.size();) {
            const auto first=changes[start];
            auto end=start;
            do {
                const auto index=changes[end++];
                const auto& object=_SceneObjects[index];
                _InstanceTransforms[index]={object.Model,object.Normal};
            }while(end<changes.size()&&changes[end]==changes[end-1]+1);
            const auto bytes=(end-start)*sizeof(InstanceVertex);
            uploads.push_back({_InstanceTransformsBuffer,first*sizeof(InstanceVertex),_InstanceTransforms.data()+first,bytes});
            _Statistics.InstanceTransformUploadBytes+=bytes;
            start=end;
        }
        if(!command.UploadBuffers(uploads.data(),static_cast<std::uint32_t>(uploads.size())))
            return Fail(DeviceError("Upload changed instance transforms"));
        ++_Statistics.InstanceBufferUploads;
    }
    _InstanceTransformsDirty=false;
    _ChangedInstanceTransforms.clear();
    return true;
}

bool Renderer::UploadInstanceStream(GRHI::ICommandBuffer& command,InstanceStream& stream) {
    if(stream.Indices.empty()||stream.Indices==stream.Uploaded)return true;
    const auto bytes=stream.Indices.size()*sizeof(std::uint32_t);
    if(!ReserveInstanceBuffer(_Device,stream.Buffer,bytes,GRHI::BufferUsage::Vertex))
        return Fail(DeviceError("Allocate visible instance index buffer"));
    const GRHI::BufferUpload upload{stream.Buffer,0,stream.Indices.data(),bytes,true};
    if(!command.UploadBuffers(&upload,1))return Fail(DeviceError("Upload visible instance indices"));
    stream.Uploaded=stream.Indices;
    ++_Statistics.InstanceBufferUploads;
    _Statistics.InstanceVisibilityUploadBytes+=bytes;
    return true;
}

bool Renderer::DrawPrepared(GRHI::IRenderPass& pass,const std::vector<PreparedDraw>& draws,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format,const std::vector<DirectionalLight>& lights,
    const void* environment,std::size_t environmentSize) {
    for(const auto& batch:_ColorInstanceDraws) {
        const auto& draw=draws[batch.First];
        if(draw.Shader) {
            if(!DrawShaderMaterial(pass,*draw.CachedMaterial,*draw.Shader,*draw.Mesh,draw.Material->Shader,
                draw.Section,*draw.Object,viewProjection,format))return false;
            ++_Statistics.DrawCalls;_Statistics.Triangles+=draw.Section.IndexCount/3;
            continue;
        }
        if(!DrawBuiltin(pass,draw,view,viewProjection,format,lights,environment,environmentSize,batch.IndexOffset,batch.Count))
            return false;
        if(batch.Count>1) {
            ++_Statistics.InstancedDrawCalls;_Statistics.InstancedInstances+=batch.Count;
            _Statistics.SavedDrawCalls+=batch.Count-1;
        }
    }
    return true;
}
}
