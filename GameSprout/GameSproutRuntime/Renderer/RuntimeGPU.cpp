#include "RuntimeRenderer.h"
#include <algorithm>
#include <vector>

namespace GameSproutRuntime {
bool Renderer::Initialize() {
    static_assert(sizeof(DrawUniform)==224,"Shader draw uniform must match std140 layout");
    static_assert(sizeof(SkinUniform)==8192,"Shader skin uniform must match 128 column-major matrices");
    const PreviewShader kinds[]={PreviewShader::MeshVertex,PreviewShader::MeshNoUVVertex,PreviewShader::MeshNoColorVertex,
        PreviewShader::MeshNoUVNoColorVertex,PreviewShader::MeshFragment,PreviewShader::GridVertex,PreviewShader::GridFragment,
        PreviewShader::SkinnedMeshVertex,PreviewShader::SkinnedMeshNoUVVertex,PreviewShader::SkinnedMeshNoColorVertex,PreviewShader::SkinnedMeshNoUVNoColorVertex};
    for(std::size_t index=0;index<7;++index) if(!_Shaders[index]) {
        _Shaders[index]=CreatePreviewShader(_Device,kinds[index]);
        if(!_Shaders[index]) return Fail(DeviceError("Create embedded Runtime preview shader"));
    }
    if(!_White) {
        const std::uint8_t pixels[]={255,255,255,255};
        GRHI::TextureDesc description;description.Width=1;description.Height=1;
        GRHI::TextureInitialData initial{pixels,sizeof(pixels),0};
        _White=_Device.CreateTexture(description,&initial);
        if(!_White) return Fail(DeviceError("Create untextured white sample"));
    }
    if(!_DefaultSampler) {
        _DefaultSampler=_Device.CreateSampler({});
        if(!_DefaultSampler) return Fail(DeviceError("Create default Runtime sampler"));
    }
    if(!_Grid) {
        struct Vertex {Vector3 Position;Color Tint;};
        static_assert(sizeof(Vertex)==28);
        std::vector<Vertex> vertices;
        vertices.reserve(404);
        for(int coordinate=-50;coordinate<=50;++coordinate) {
            const Color gray=coordinate%10==0?Color{0.24f,0.27f,0.31f}:Color{0.15f,0.17f,0.20f};
            const Color x=coordinate==0?Color{0.60f,0.19f,0.17f}:gray;
            const Color z=coordinate==0?Color{0.17f,0.32f,0.65f}:gray;
            vertices.push_back({{-50,0,float(coordinate)},x});vertices.push_back({{50,0,float(coordinate)},x});
            vertices.push_back({{float(coordinate),0,-50},z});vertices.push_back({{float(coordinate),0,50},z});
        }
        GRHI::BufferDesc description;description.Size=vertices.size()*sizeof(Vertex);description.Usage=GRHI::BufferUsage::Vertex;
        GRHI::BufferInitialData initial{vertices.data(),description.Size};
        _Grid=_Device.CreateBuffer(description,&initial);
        if(!_Grid) return Fail(DeviceError("Create Runtime reference grid"));
        _GridVertices=static_cast<std::uint32_t>(vertices.size());
    }
    return true;
}
GRHI::IGraphicsPipeline* Renderer::Pipeline(const PipelineKey& key) {
    const auto found=_Pipelines.find(key);
    if(found!=_Pipelines.end()) return found->second;
    GRHI::VertexBindingDesc binding;binding.Binding=0;binding.Stride=key.Grid?28:key.Stride;
    GRHI::VertexAttributeDesc attributes[6];
    attributes[0]={0,0,GRHI::VertexFormat::Float3,key.Grid?0:key.Position};
    std::size_t count=1;
    if(key.Grid) attributes[count++]={1,0,GRHI::VertexFormat::Float4,12};
    else {
        attributes[count++]={1,0,GRHI::VertexFormat::Float3,key.Normal};
        if(key.Color>=0) attributes[count++]={2,0,GRHI::VertexFormat::Float4,static_cast<std::uint32_t>(key.Color)};
        if(key.UV>=0) attributes[count++]={3,0,GRHI::VertexFormat::Float2,static_cast<std::uint32_t>(key.UV)};
        if(key.BoneIndices>=0) {
            attributes[count++]={4,0,GRHI::VertexFormat::UInt32x4,static_cast<std::uint32_t>(key.BoneIndices)};
            attributes[count++]={5,0,GRHI::VertexFormat::Float4,static_cast<std::uint32_t>(key.BoneWeights)};
        }
    }
    const std::size_t variant=key.Color<0?(key.UV<0?3:2):(key.UV<0?1:0);
    const std::size_t shader=key.Grid?5:variant+(key.BoneIndices>=0?7:0);
    if(!_Shaders[shader]) {
        _Shaders[shader]=CreatePreviewShader(_Device,static_cast<PreviewShader>(shader));
        if(!_Shaders[shader]) {Fail(DeviceError("Create Runtime skinned vertex shader"));return nullptr;}
    }
    GRHI::GraphicsPipelineDesc description;
    description.VertexShader=_Shaders[shader];description.FragmentShader=_Shaders[key.Grid?6:4];
    description.ColorFormat=key.Format;description.Primitive=key.Grid&&!key.DebugTriangles?GRHI::PrimitiveType::LineList:GRHI::PrimitiveType::TriangleList;
    if(key.Grid) description.Blend=GRHI::BlendMode::Alpha;
    description.Cull=key.Grid || key.DoubleSided?GRHI::CullMode::None:GRHI::CullMode::Back;
    description.Winding=key.Negative?GRHI::FrontFace::Clockwise:GRHI::FrontFace::CounterClockwise;
    description.VertexBindings=&binding;description.VertexBindingCount=1;
    description.VertexAttributes=attributes;description.VertexAttributeCount=count;
    description.DepthFormat=GRHI::TextureFormat::D32_Float;description.DepthTest=key.DepthTest;description.DepthWrite=!key.Grid;
    description.DepthCompare=GRHI::CompareOp::LessEqual;
    auto [position,inserted]=_Pipelines.emplace(key,nullptr);
    position->second=_Device.CreateGraphicsPipeline(description);
    if(!position->second) {_Pipelines.erase(position);Fail(DeviceError("Create Runtime graphics pipeline"));return nullptr;}
    return position->second;
}
GRHI::ITexture* Renderer::DepthTarget(std::uint32_t width,std::uint32_t height) {
    const auto key=std::make_pair(width,height);
    auto found=_Depths.find(key);
    if(found!=_Depths.end()) {found->second.LastUsed=_Frame;return found->second.Texture;}
    GRHI::TextureDesc description;description.Width=width;description.Height=height;
    description.Format=GRHI::TextureFormat::D32_Float;description.Usage=GRHI::TextureUsage::DepthStencilTarget;
    if(_Depths.size()>=4) {
        auto oldest=std::min_element(_Depths.begin(),_Depths.end(),[](const auto& left,const auto& right) {return left.second.LastUsed<right.second.LastUsed;});
        delete oldest->second.Texture;_Depths.erase(oldest);
    }
    auto [position,inserted]=_Depths.emplace(key,Depth{});
    position->second.Texture=_Device.CreateTexture(description);
    position->second.LastUsed=_Frame;
    if(!position->second.Texture) {_Depths.erase(position);Fail(DeviceError("Create Runtime depth target"));return nullptr;}
    return position->second.Texture;
}
bool Renderer::DrawGrid(GRHI::IRenderPass& pass,const Mat4& viewProjection,GRHI::TextureFormat format) {
    PipelineKey key;key.Format=format;key.Grid=true;
    auto* pipeline=Pipeline(key);if(!pipeline) return false;
    DrawUniform uniform;uniform.ViewProjection=viewProjection;
    pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,_Grid,0);
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));pass.Draw(_GridVertices);
    ++_Statistics.DrawCalls;return true;
}
}
