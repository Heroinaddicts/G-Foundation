#include "RuntimeRenderer.h"
#include "RuntimeRenderResourceCache.h"
#include "RuntimeMath.h"
#include "Resources/BuiltinMeshes.h"
#include <cmath>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
bool Attribute(std::uint32_t stride,std::int64_t offset,std::uint32_t size) {
    return offset>=0 && static_cast<std::uint64_t>(offset)+size<=stride && offset%4==0;
}
std::uint32_t Read32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
}
bool ValidateMesh(GameSproutRuntime::MeshRenderData& data,std::string& error) {
    if (!data.VertexCount || !data.VertexStride || !data.IndexCount || data.IndexCount%3 || data.Bytes.size()>512u*1024u*1024u ||
        !Attribute(data.VertexStride,data.PositionOffset,12) || !Attribute(data.VertexStride,data.NormalOffset,12) ||
        (data.ColorOffset!=-1 && !Attribute(data.VertexStride,data.ColorOffset,16)) ||
        (data.UVOffset!=-1 && !Attribute(data.VertexStride,data.UVOffset,8)) ||
        (data.BoneIndicesOffset!=-1 && !Attribute(data.VertexStride,data.BoneIndicesOffset,16)) ||
        (data.BoneWeightsOffset!=-1 && !Attribute(data.VertexStride,data.BoneWeightsOffset,16)) ||
        ((data.BoneIndicesOffset<0)!=(data.BoneWeightsOffset<0)) ||
        (!data.Bones.empty() && data.BoneIndicesOffset<0) ||
        data.IndexOffset%4 || std::uint64_t(data.VertexCount)*data.VertexStride>data.IndexOffset ||
        data.IndexOffset>data.Bytes.size() || std::uint64_t(data.IndexCount)*4>data.Bytes.size()-data.IndexOffset) {
        error="Invalid prepared mesh byte layout, counts or offsets";return false;
    }
    for(const auto& bone:data.Bones) if(bone.NodePath.empty() || !GameSproutRuntime::RuntimeMath::IsFinite(bone.InverseBindMatrix)) {
        error="Invalid prepared mesh bone path or inverse bind matrix";return false;
    }
    if(data.BoneIndicesOffset>=0) for(std::uint32_t vertex=0;vertex<data.VertexCount;++vertex) {
        const auto* indices=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+data.BoneIndicesOffset;
        const auto* weights=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+data.BoneWeightsOffset;
        double total=0;
        for(std::size_t influence=0;influence<4;++influence) {
            const auto bone=Read32(indices+influence*4);
            const float weight=std::bit_cast<float>(Read32(weights+influence*4));
            if(!std::isfinite(weight) || weight<0 || weight>1 ||
               (!data.Bones.empty() && bone>=data.Bones.size()) ||
               (data.Bones.empty() && (bone!=0 || weight!=0))) {
                error="Invalid prepared mesh bone index or weight";return false;
            }
            total+=weight;
        }
        if(!data.Bones.empty() && std::fabs(total-1.0)>0.001) {
            error="Prepared skin weights must be normalized and bind every vertex";return false;
        }
    }
    for (std::uint32_t index=0;index<data.IndexCount;++index) {
        const auto* bytes=data.Bytes.data()+data.IndexOffset+std::uint64_t(index)*4;
        const auto value=Read32(bytes);
        if (value>=data.VertexCount) {error="Prepared mesh index exceeds vertex count";return false;}
    }
    const std::int64_t offsets[]={data.PositionOffset,data.NormalOffset,data.ColorOffset,data.UVOffset};
    const int dimensions[]={3,3,4,2};
    for(std::uint32_t vertex=0;vertex<data.VertexCount;++vertex) for(int attribute=0;attribute<4;++attribute) {
        if(offsets[attribute]<0) continue;
        const auto* start=data.Bytes.data()+std::uint64_t(vertex)*data.VertexStride+offsets[attribute];
        for(int component=0;component<dimensions[attribute];++component) if(!std::isfinite(std::bit_cast<float>(Read32(start+component*4)))) {
            error="Prepared mesh contains nonfinite vertex attributes";return false;
        }
    }
    if(!GameSproutRuntime::RuntimeMath::IsFinite(data.LocalBounds.Center) || !GameSproutRuntime::RuntimeMath::IsFinite(data.LocalBounds.Extents) ||
       data.LocalBounds.Extents.X<0 || data.LocalBounds.Extents.Y<0 || data.LocalBounds.Extents.Z<0) {error="Invalid prepared mesh local bounds";return false;}
    if (data.Sections.empty()) data.Sections.push_back({0,data.IndexCount,0});
    for (const auto& section:data.Sections) if (section.FirstIndex%3 || section.IndexCount%3 ||
        section.FirstIndex>data.IndexCount || section.IndexCount>data.IndexCount-section.FirstIndex) {
        error="Prepared mesh section exceeds index data";return false;
    }
    return true;
}
}
namespace GameSproutRuntime {
namespace {
GRHI::SamplerDesc ToGRHISampler(const SamplerSettings& settings) {
    GRHI::SamplerDesc result;
    result.MinFilter=settings.MinFilter==TextureFilter::Nearest?GRHI::FilterMode::Nearest:GRHI::FilterMode::Linear;
    result.MagFilter=settings.MagFilter==TextureFilter::Nearest?GRHI::FilterMode::Nearest:GRHI::FilterMode::Linear;
    const auto address=[](TextureAddressMode mode){switch(mode){case TextureAddressMode::Repeat:return GRHI::AddressMode::Repeat;case TextureAddressMode::ClampToEdge:return GRHI::AddressMode::ClampToEdge;case TextureAddressMode::MirroredRepeat:return GRHI::AddressMode::MirroredRepeat;}return GRHI::AddressMode::ClampToEdge;};
    result.AddressU=address(settings.AddressU);result.AddressV=address(settings.AddressV);return result;
}
}

Renderer::Renderer(Runtime& runtime,GRHI::IDevice& device,IRuntimeResourceProvider& resources)
    :_Runtime(runtime),_Device(device),_Resources(resources),
     _ResourceLease(runtime,device,resources),_ResourceCache(_ResourceLease.Get()),
     _Meshes(_ResourceCache.Meshes),_Textures(_ResourceCache.Textures),_Materials(_ResourceCache.Materials),
     _UserShaders(_ResourceCache.UserShaders),_ShaderPipelines(_ResourceCache.ShaderPipelines),_Pipelines(_ResourceCache.Pipelines),
     _Shaders(_ResourceCache.Shaders),_White(_ResourceCache.White),_DefaultSampler(_ResourceCache.DefaultSampler),
     _Grid(_ResourceCache.Grid),_GridVertices(_ResourceCache.GridVertices) {}
Renderer::~Renderer() {
    ClearResources();
    for(auto& entry:_Depths) delete entry.second.Texture;
    delete _ShadowAtlas;delete _ShadowSampler;
    delete _ColorTarget;
    if(_Surface){_Surface->Release();delete _Surface;}
}
void Renderer::ClearResources() {
    _ShadowValid=false;_ShadowUniform.Info={};
    _PreparedDraws.clear();_SceneObjects.clear();_SceneLights.clear();
    _SortedMeshCount=0;_SortedMaterialCount=0;
    _ObjectSnapshotKnown=false;_LightSnapshotKnown=false;
    _PreparedKnown=false;_SnapshotHasSkin=false;_PreparedDiagnostic.clear();
    _ExtractionDiagnostic.clear();_LightDiagnostic.clear();
    ClearInstanceBuffers();
    _ColliderMeshes.clear();
}
void Renderer::SyncRevision() {
    _ResourceCache.Synchronize();
    if(_ResourceGeneration!=_ResourceCache.Generation) {
        // A sibling viewport may already have released the previous shared
        // generation. Discard every local borrowed address before looking up
        // resources in the new maps; private render targets remain independent.
        ClearResources();_ResourceGeneration=_ResourceCache.Generation;
    }
}
std::string Renderer::DeviceError(const std::string& operation) const {
    const auto& error=_Device.GetLastError();return operation+(error.empty()?"":": "+error);
}
bool Renderer::Fail(const std::string& error) {_Error=error;return false;}
void Renderer::Diagnostic(const std::string& error) {
    if(error.empty() || _Error.size()>4096) return;
    if(!_Error.empty()) _Error+='\n';_Error+=error;
}
Renderer::MeshGPU* Renderer::Mesh(const ResourceReference& reference) {
    if(reference.Guid.empty() || !reference.LocalID) {Diagnostic("Mesh reference is unbound or invalid");return nullptr;}
    auto [position,inserted]=_Meshes.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=IsBuiltinResource(reference)?0:_Resources.GetResourceRevision(reference);
            const bool loaded=IsBuiltinResource(reference)?BuiltinMeshes::Load(reference,entry.Data,entry.Error):
                _Resources.LoadMesh(reference,entry.Data,entry.Error);
            if(loaded && ValidateMesh(entry.Data,entry.Error)) {
                // 仅在载入/修订变化时解包一次；GPU 上传后丢弃交错原始字节，保留紧密拾取数据。
                entry.Positions.reserve(entry.Data.VertexCount);
                for(std::uint32_t vertex=0;vertex<entry.Data.VertexCount;++vertex) {
                    const auto* bytes=entry.Data.Bytes.data()+std::uint64_t(vertex)*entry.Data.VertexStride+entry.Data.PositionOffset;
                    entry.Positions.push_back({std::bit_cast<float>(Read32(bytes)),std::bit_cast<float>(Read32(bytes+4)),std::bit_cast<float>(Read32(bytes+8))});
                    if(!entry.Data.Bones.empty()) {
                        MeshGPU::SkinVertex skin;
                        const auto* source=entry.Data.Bytes.data()+std::uint64_t(vertex)*entry.Data.VertexStride;
                        for(std::size_t influence=0;influence<4;++influence) {
                            skin.Bones[influence]=Read32(source+entry.Data.BoneIndicesOffset+influence*4);
                            skin.Weights[influence]=std::bit_cast<float>(Read32(source+entry.Data.BoneWeightsOffset+influence*4));
                        }
                        entry.Skin.push_back(skin);
                    }
                }
                entry.Indices.reserve(entry.Data.IndexCount);
                for(std::uint32_t index=0;index<entry.Data.IndexCount;++index)
                    entry.Indices.push_back(Read32(entry.Data.Bytes.data()+entry.Data.IndexOffset+std::uint64_t(index)*4));
                GRHI::BufferDesc description;description.Size=entry.Data.Bytes.size();description.Usage=GRHI::BufferUsage::Vertex|GRHI::BufferUsage::Index;
                GRHI::BufferInitialData initial{entry.Data.Bytes.data(),entry.Data.Bytes.size()};
                entry.Buffer=_Device.CreateBuffer(description,&initial);
                if(!entry.Buffer) entry.Error=DeviceError("Create prepared mesh buffer");
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();}
        catch(...) {entry.Error="Resource provider threw while loading mesh";}
        std::vector<std::uint8_t>().swap(entry.Data.Bytes);
        if(!entry.Buffer) {entry.Positions.clear();entry.Skin.clear();entry.Indices.clear();}
    }
    if(!entry.Buffer) {Diagnostic("Mesh "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::TextureGPU* Renderer::Texture(const ResourceReference& reference) {
    if(reference.Guid.empty() || !reference.LocalID) {Diagnostic("Texture reference is invalid");return nullptr;}
    auto [position,inserted]=_Textures.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=_Resources.GetResourceRevision(reference);
            TextureRenderData data;
            if(_Resources.LoadTexture(reference,data,entry.Error)) {
                if(!data.Width || !data.Height || std::uint64_t(data.Width)*data.Height>128u*1024u*1024u ||
                    std::uint64_t(data.Width)*data.Height*4!=data.Pixels.size()) entry.Error="Invalid tightly packed RGBA8 texture";
                else {
                    GRHI::TextureDesc description;description.Width=data.Width;description.Height=data.Height;
                    description.Format=data.SRGB?GRHI::TextureFormat::RGBA8_UNorm_sRGB:GRHI::TextureFormat::RGBA8_UNorm;
                    entry.SRGB=data.SRGB;
                    description.Usage=GRHI::TextureUsage::Sampled;
                    GRHI::TextureInitialData initial{data.Pixels.data(),data.Pixels.size(),0};
                    entry.Texture=_Device.CreateTexture(description,&initial);
                    if(entry.Texture) entry.Sampler=_Device.CreateSampler(ToGRHISampler(data.Sampler));
                    if(!entry.Texture || !entry.Sampler) {entry.Error=DeviceError("Create prepared texture/sampler");delete entry.Texture;delete entry.Sampler;entry.Texture=nullptr;entry.Sampler=nullptr;}
                }
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();}
        catch(...) {entry.Error="Resource provider threw while loading texture";}
        if(!entry.Texture || !entry.Sampler) {delete entry.Texture;delete entry.Sampler;entry.Texture=nullptr;entry.Sampler=nullptr;}
    }
    if(!entry.Texture) {Diagnostic("Texture "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::Material* Renderer::MaterialData(const ResourceReference& reference) {
    auto [position,inserted]=_Materials.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            entry.ResourceRevision=_Resources.GetResourceRevision(reference);
            entry.Loaded=_Resources.LoadMaterial(reference,entry.Data,entry.Error);
            if(entry.Loaded && (!RuntimeMath::IsFinite(entry.Data.BaseColor) ||
                entry.Data.BaseColor.R<0 || entry.Data.BaseColor.G<0 || entry.Data.BaseColor.B<0 ||
                entry.Data.BaseColor.A<0 || entry.Data.BaseColor.A>1 ||
                (entry.Data.BaseColorTexture.Guid.empty()!= (entry.Data.BaseColorTexture.LocalID==0)) ||
                (entry.Data.Shader.Guid.empty()!= (entry.Data.Shader.LocalID==0)) ||
                static_cast<unsigned>(entry.Data.ShadingModel)>static_cast<unsigned>(MaterialShadingModel::Lit)||
                entry.Data.SortingPriority < -50 || entry.Data.SortingPriority > 50)) {
                entry.Error="Invalid prepared material color or texture identity";entry.Loaded=false;
            }
            if(entry.Loaded&&entry.Data.ShadingModel==MaterialShadingModel::Lit) {
                const auto& data=entry.Data;
                const auto finite=[](Vector4 value){return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z)&&std::isfinite(value.W);};
                if(!std::isfinite(data.Metallic)||data.Metallic<0||data.Metallic>1||
                    !std::isfinite(data.Roughness)||data.Roughness<0||data.Roughness>1||
                    !std::isfinite(data.NormalScale)||data.NormalScale<0||
                    !RuntimeMath::IsFinite(data.EmissionColor)||data.EmissionColor.R<0||data.EmissionColor.G<0||data.EmissionColor.B<0||
                    !finite(data.BaseColorTextureST)||!finite(data.NormalTextureST)||
                    (data.NormalTexture.Guid.empty()!=(data.NormalTexture.LocalID==0))||!data.Shader.Guid.empty()) {
                    entry.Error="Invalid prepared Lit material parameters, normal map or conflicting user Shader";entry.Loaded=false;
                }
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();entry.Loaded=false;}
        catch(...) {entry.Error="Resource provider threw while loading material";entry.Loaded=false;}
    }
    if(!entry.Loaded) {Diagnostic("Material "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::TextureGPU* Renderer::MaterialTexture(Material& material,bool normal) {
    auto*& texture=normal?material.NormalTexture:material.BaseColorTexture;
    bool& attempted=normal?material.NormalAttempted:material.BaseColorAttempted;
    auto& error=normal?material.NormalError:material.BaseColorError;
    if(!attempted) {
        attempted=true;
        const auto diagnosticBefore=_Error.size();
        texture=Texture(normal?material.Data.NormalTexture:material.Data.BaseColorTexture);
        error=_Error.substr(diagnosticBefore);
        if(!error.empty()&&error.front()=='\n')error.erase(0,1);
    }else if(!texture)Diagnostic(error);
    return texture;
}
}
