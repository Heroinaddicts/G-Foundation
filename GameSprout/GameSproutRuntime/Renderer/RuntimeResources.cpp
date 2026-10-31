#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
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
        data.IndexOffset%4 || std::uint64_t(data.VertexCount)*data.VertexStride>data.IndexOffset ||
        data.IndexOffset>data.Bytes.size() || std::uint64_t(data.IndexCount)*4>data.Bytes.size()-data.IndexOffset) {
        error="Invalid prepared mesh byte layout, counts or offsets";return false;
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
Renderer::Renderer(Runtime& runtime,GRHI::IDevice& device,IRuntimeResourceProvider& resources)
    :_Runtime(runtime),_Device(device),_Resources(resources) {}
Renderer::~Renderer() {
    ClearResources();
    for(auto& entry:_Pipelines) delete entry.second;
    for(auto& entry:_Depths) delete entry.second.Texture;
    for(auto* shader:_Shaders) delete shader;
    delete _White;delete _DefaultSampler;delete _Grid;
}
void Renderer::ClearResources() {
    for(auto& entry:_Meshes) delete entry.second.Buffer;
    for(auto& entry:_Textures) {delete entry.second.Texture;delete entry.second.Sampler;}
    _Meshes.clear();_Textures.clear();_Materials.clear();
}
void Renderer::SyncRevision() {
    const auto revision=_Resources.GetRevision();
    if(!_RevisionKnown || revision!=_Revision) {ClearResources();_Revision=revision;_RevisionKnown=true;}
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
            if(_Resources.LoadMesh(reference,entry.Data,entry.Error) && ValidateMesh(entry.Data,entry.Error)) {
                // 仅在载入/修订变化时解包一次；GPU 上传后丢弃交错原始字节，保留紧密拾取数据。
                entry.Positions.reserve(entry.Data.VertexCount);
                for(std::uint32_t vertex=0;vertex<entry.Data.VertexCount;++vertex) {
                    const auto* bytes=entry.Data.Bytes.data()+std::uint64_t(vertex)*entry.Data.VertexStride+entry.Data.PositionOffset;
                    entry.Positions.push_back({std::bit_cast<float>(Read32(bytes)),std::bit_cast<float>(Read32(bytes+4)),std::bit_cast<float>(Read32(bytes+8))});
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
        if(!entry.Buffer) {entry.Positions.clear();entry.Indices.clear();}
    }
    if(!entry.Buffer) {Diagnostic("Mesh "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
Renderer::TextureGPU* Renderer::Texture(const ResourceReference& reference) {
    if(reference.Guid.empty() || !reference.LocalID) {Diagnostic("Texture reference is invalid");return nullptr;}
    auto [position,inserted]=_Textures.try_emplace(ResourceKey(reference));auto& entry=position->second;
    if(inserted) {
        try {
            TextureRenderData data;
            if(_Resources.LoadTexture(reference,data,entry.Error)) {
                if(!data.Width || !data.Height || std::uint64_t(data.Width)*data.Height>128u*1024u*1024u ||
                    std::uint64_t(data.Width)*data.Height*4!=data.Pixels.size()) entry.Error="Invalid tightly packed RGBA8 texture";
                else {
                    GRHI::TextureDesc description;description.Width=data.Width;description.Height=data.Height;
                    description.Format=data.SRGB?GRHI::TextureFormat::RGBA8_UNorm_sRGB:GRHI::TextureFormat::RGBA8_UNorm;
                    description.Usage=GRHI::TextureUsage::Sampled;
                    GRHI::TextureInitialData initial{data.Pixels.data(),data.Pixels.size(),0};
                    entry.Texture=_Device.CreateTexture(description,&initial);
                    if(entry.Texture) entry.Sampler=_Device.CreateSampler(data.Sampler);
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
            entry.Loaded=_Resources.LoadMaterial(reference,entry.Data,entry.Error);
            if(entry.Loaded && (!RuntimeMath::IsFinite(entry.Data.BaseColor) ||
                entry.Data.BaseColor.R<0 || entry.Data.BaseColor.G<0 || entry.Data.BaseColor.B<0 ||
                entry.Data.BaseColor.A<0 || entry.Data.BaseColor.A>1 ||
                (entry.Data.BaseColorTexture.Guid.empty()!= (entry.Data.BaseColorTexture.LocalID==0)))) {
                entry.Error="Invalid prepared material color or texture identity";entry.Loaded=false;
            }
        } catch(const std::exception& exception) {entry.Error=exception.what();entry.Loaded=false;}
        catch(...) {entry.Error="Resource provider threw while loading material";entry.Loaded=false;}
    }
    if(!entry.Loaded) {Diagnostic("Material "+reference.Guid+"_"+std::to_string(reference.LocalID)+": "+entry.Error);return nullptr;}
    return &entry;
}
}
