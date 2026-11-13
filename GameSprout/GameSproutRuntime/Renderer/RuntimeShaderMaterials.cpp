#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
#include "Resources/BuiltinMeshes.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <set>

namespace {
using namespace GameSproutRuntime;
unsigned Components(ShaderValueType type) {
    switch(type){case ShaderValueType::Float:return 1;case ShaderValueType::Float2:return 2;
    case ShaderValueType::Float3:return 3;case ShaderValueType::Float4:return 4;default:return 0;}
}
bool PackBlock(const ShaderUniformBlock& block,const MaterialRenderData& material,std::vector<std::uint8_t>& bytes,std::string& error) {
    bytes.assign(block.Size,0);
    for(const auto& member:block.Members) {
        const auto found=std::find_if(material.Properties.begin(),material.Properties.end(),[&](const auto& value){return value.Name==member.Name;});
        if(found==material.Properties.end()||found->Type!=member.Type){error="Material property is missing or has the wrong Shader type: "+member.Name;return false;}
        auto* target=bytes.data()+member.Offset;
        if(member.Type==ShaderValueType::Int)std::memcpy(target,&found->Integer,4);
        else if(member.Type==ShaderValueType::UInt)std::memcpy(target,&found->Unsigned,4);
        else {
            const auto count=Components(member.Type);
            for(unsigned index=0;index<count;++index)if(!std::isfinite(found->Numbers[index])){error="Material property is nonfinite: "+member.Name;return false;}
            std::memcpy(target,found->Numbers,count*4);
        }
    }
    return true;
}
float ReadFloat(const std::uint8_t* data) {
    const std::uint32_t bits=std::uint32_t(data[0])|(std::uint32_t(data[1])<<8)|(std::uint32_t(data[2])<<16)|(std::uint32_t(data[3])<<24);
    return std::bit_cast<float>(bits);
}
bool AttributeValid(const MeshRenderData& data,std::int32_t offset,std::uint32_t size) {
    return offset>=0&&std::uint32_t(offset)<=data.VertexStride&&size<=data.VertexStride-std::uint32_t(offset);
}
}

namespace GameSproutRuntime {
bool Renderer::PrepareShaderMaterial(Material& material,ShaderGPU& shader) {
    if(material.ShaderAttempted) {
        if(!material.ShaderReady)Diagnostic(material.Error);
        return material.ShaderReady;
    }
    material.ShaderAttempted=true;
    const auto fail=[&](const std::string& error){material.Error=error;Diagnostic(error);return false;};
    if(material.Data.Properties.size()>512||material.Data.Textures.size()>64)return fail("Prepared material has too many properties");
    std::set<std::string> names;
    for(const auto& value:material.Data.Properties)if(value.Name.empty()||!names.insert(value.Name).second)
        return fail("Prepared material has duplicate or empty property names");
    names.clear();
    for(const auto& value:material.Data.Textures)if(value.Name.empty()||!names.insert(value.Name).second||
        (value.Texture.Guid.empty()!=(value.Texture.LocalID==0)))return fail("Prepared material has invalid texture identities");
    // Keep the custom shader's blend and sampling semantics while a texture
    // is pending. Resolve bindings before packing and retry when it publishes.
    if(_TexturePrefetchEnabled) {
        bool pending=false;
        for(const auto& binding:shader.Data.Passes[0].Fragment.Textures) {
            const auto found=std::find_if(material.Data.Textures.begin(),material.Data.Textures.end(),
                [&](const auto& value){return value.Name==binding.Name;});
            if(found!=material.Data.Textures.end())pending|=PrefetchTexture(found->Texture,true);
        }
        if(pending){material.ShaderAttempted=false;return false;}
    }
    const auto pack=[&](const ShaderProgramData& program,std::vector<BoundUniform>& output) {
        for(const auto& block:program.Uniforms) {
            if(block.Source!=ShaderUniformSource::Material)continue;
            BoundUniform uniform;uniform.Slot=block.Slot;
            if(!PackBlock(block,material.Data,uniform.Bytes,material.Error))return false;
            output.push_back(std::move(uniform));
        }
        return true;
    };
    const auto& pass=shader.Data.Passes[0];
    if(!pack(pass.Vertex,material.VertexUniforms)||!pack(pass.Fragment,material.FragmentUniforms))return fail(material.Error);
    for(const auto& binding:pass.Fragment.Textures) {
        const auto found=std::find_if(material.Data.Textures.begin(),material.Data.Textures.end(),[&](const auto& value){return value.Name==binding.Name;});
        if(found==material.Data.Textures.end())return fail("Material texture property is missing: "+binding.Name);
        TextureGPU* texture=nullptr;
        if(!found->Texture.Guid.empty()) {
            texture=Texture(found->Texture);
            if(!texture)return fail("Material texture could not be loaded: "+binding.Name);
        }
        material.ShaderTextures.push_back(texture);
    }
    material.ShaderReady=true;return true;
}

bool Renderer::EnsureShaderVertices(MeshGPU& mesh,const ResourceReference& reference) {
    if(mesh.ShaderVertexBuffer)return true;
    // Reload prepared bytes once on first custom-Shader use, rather than keep a
    // second CPU mesh or allocate a second GPU buffer for every preview mesh.
    MeshRenderData source;std::string error;
    const bool loaded=IsBuiltinResource(reference)?BuiltinMeshes::Load(reference,source,error):_Resources.LoadMesh(reference,source,error);
    if(!loaded){Diagnostic("User Shader mesh: "+error);return false;}
    if(source.VertexCount!=mesh.Data.VertexCount||source.VertexStride!=mesh.Data.VertexStride||
        source.PositionOffset!=mesh.Data.PositionOffset||source.NormalOffset!=mesh.Data.NormalOffset||
        source.ColorOffset!=mesh.Data.ColorOffset||source.UVOffset!=mesh.Data.UVOffset||
        !source.VertexCount||!source.VertexStride||source.Bytes.size()>512u*1024u*1024u||
        std::uint64_t(source.VertexCount)*source.VertexStride>source.Bytes.size()||
        !AttributeValid(source,source.PositionOffset,12)||!AttributeValid(source,source.NormalOffset,12)||
        (source.ColorOffset!=-1&&!AttributeValid(source,source.ColorOffset,16))||
        (source.UVOffset!=-1&&!AttributeValid(source,source.UVOffset,8))) {
        Diagnostic("Prepared mesh layout changed without a resource revision");return false;
    }
    struct Vertex {Vector3 Position,Normal;Color Tint;Vector2 UV;};
    static_assert(sizeof(Vertex)==48);
    std::vector<Vertex> vertices;vertices.reserve(source.VertexCount);
    for(std::uint32_t index=0;index<source.VertexCount;++index) {
        const auto* bytes=source.Bytes.data()+std::uint64_t(index)*source.VertexStride;Vertex vertex;
        const auto vector=[&](std::uint32_t offset){return Vector3{ReadFloat(bytes+offset),ReadFloat(bytes+offset+4),ReadFloat(bytes+offset+8)};};
        vertex.Position=vector(source.PositionOffset);vertex.Normal=vector(source.NormalOffset);
        if(source.ColorOffset>=0){const auto* color=bytes+source.ColorOffset;vertex.Tint={ReadFloat(color),ReadFloat(color+4),ReadFloat(color+8),ReadFloat(color+12)};}
        if(source.UVOffset>=0)vertex.UV={ReadFloat(bytes+source.UVOffset),ReadFloat(bytes+source.UVOffset+4)};
        if(!RuntimeMath::IsFinite(vertex.Position)||!RuntimeMath::IsFinite(vertex.Normal)||!RuntimeMath::IsFinite(vertex.Tint)||
            !std::isfinite(vertex.UV.X)||!std::isfinite(vertex.UV.Y)){Diagnostic("User Shader mesh has nonfinite vertex data");return false;}
        vertices.push_back(vertex);
    }
    GRHI::BufferDesc description;description.Size=vertices.size()*sizeof(Vertex);description.Usage=GRHI::BufferUsage::Vertex;
    const GRHI::BufferInitialData initial{vertices.data(),description.Size};
    mesh.ShaderVertexBuffer=_Device.CreateBuffer(description,&initial);
    if(!mesh.ShaderVertexBuffer){Diagnostic(DeviceError("Create canonical user Shader vertex buffer"));return false;}
    return true;
}

bool Renderer::DrawShaderMaterial(GRHI::IRenderPass& target,Material& material,ShaderGPU& shader,MeshGPU& mesh,
    const ResourceReference& shaderReference,const MeshSection& section,const DrawObject& object,const Mat4& viewProjection,GRHI::TextureFormat format) {
    auto* pipeline=ShaderPipeline(shaderReference,shader,format,object.Negative);if(!pipeline)return false;
    target.SetPipeline(pipeline);target.SetVertexBuffer(0,mesh.ShaderVertexBuffer);
    target.SetIndexBuffer(mesh.Buffer,GRHI::IndexFormat::UInt32,static_cast<std::size_t>(mesh.Data.IndexOffset));
    DrawUniform draw;draw.Model=object.Model;draw.ViewProjection=viewProjection;draw.Normal=object.Normal;
    auto frame=_ShaderFrame;
    for(const auto& light:_SceneLights)if(light.CullingMask&(std::uint32_t{1}<<object.Layer)) {
        frame.MainLightDirection={light.TowardsLight.X,light.TowardsLight.Y,light.TowardsLight.Z,1};
        frame.MainLightColor={light.Radiance.R,light.Radiance.G,light.Radiance.B,1};
        break;
    }
    bool animated=false;
    for(const auto& block:shader.Data.Passes[0].Vertex.Uniforms)if(block.Source==ShaderUniformSource::Draw)
        target.SetVertexUniform(block.Slot,&draw,block.Size);
    for(const auto& block:shader.Data.Passes[0].Vertex.Uniforms)if(block.Source==ShaderUniformSource::Frame) {
        target.SetVertexUniform(block.Slot,&frame,sizeof(frame));animated=true;
    }
    for(const auto& block:shader.Data.Passes[0].Fragment.Uniforms)if(block.Source==ShaderUniformSource::Frame) {
        target.SetFragmentUniform(block.Slot,&frame,sizeof(frame));animated=true;
    }
    for(const auto& uniform:material.VertexUniforms)target.SetVertexUniform(uniform.Slot,uniform.Bytes.data(),uniform.Bytes.size());
    for(const auto& uniform:material.FragmentUniforms)target.SetFragmentUniform(uniform.Slot,uniform.Bytes.data(),uniform.Bytes.size());
    for(std::size_t slot=0;slot<material.ShaderTextures.size();++slot) {
        auto* texture=material.ShaderTextures[slot];
        target.SetFragmentTextureSampler(static_cast<std::uint32_t>(slot),texture?texture->Texture:_White,texture?texture->Sampler:_DefaultSampler);
    }
    target.DrawIndexed(section.IndexCount,1,section.FirstIndex);
    _Statistics.RequiresContinuousRendering=_Statistics.RequiresContinuousRendering||animated;
    return true;
}
}
