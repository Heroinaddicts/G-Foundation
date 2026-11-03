#include "RuntimeRenderer.h"
#include <algorithm>

namespace GameSproutRuntime {
bool Renderer::DrawBuiltin(GRHI::IRenderPass& pass,const PreparedDraw& draw,const RenderView& view,
    const Mat4& viewProjection,GRHI::TextureFormat format,const std::vector<DirectionalLight>& lights,
    const void* environment,std::size_t environmentSize,GRHI::IBuffer* instances,std::uint32_t instanceCount) {
    const auto& data=draw.Mesh->Data;
    PipelineKey key;key.Format=format;key.Stride=data.VertexStride;key.Position=data.PositionOffset;key.Normal=data.NormalOffset;
    key.Color=data.ColorOffset;key.UV=data.UVOffset;key.DoubleSided=draw.Material->DoubleSided;key.Negative=draw.Object->Negative;
    key.Lit=draw.Material->ShadingModel==MaterialShadingModel::Lit;key.Instanced=instances!=nullptr;
    if(draw.Object->Skinned){key.BoneIndices=data.BoneIndicesOffset;key.BoneWeights=data.BoneWeightsOffset;}
    auto* pipeline=Pipeline(key);if(!pipeline)return false;
    DrawUniform uniform;uniform.ViewProjection=viewProjection;
    // Instance transforms live in the resident vertex stream; keeping identity
    // here makes the common draw block reusable across different instance sets.
    if(!instances){uniform.Model=draw.Object->Model;uniform.Normal=draw.Object->Normal;}
    uniform.BaseColor=draw.Material->BaseColor;uniform.TextureST=draw.Material->BaseColorTextureST;
    pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,draw.Mesh->Buffer,0);
    if(instances)pass.SetVertexBuffer(1,instances,0);
    pass.SetIndexBuffer(draw.Mesh->Buffer,GRHI::IndexFormat::UInt32,static_cast<std::size_t>(data.IndexOffset));
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));
    if(draw.Object->Skinned) {
        SkinUniform skin;
        std::copy(draw.Object->SkinMatrices.begin(),draw.Object->SkinMatrices.end(),skin.Bones);
        pass.SetVertexUniform(1,&skin,sizeof(skin));
    }
    pass.SetFragmentTextureSampler(0,draw.Texture?draw.Texture->Texture:_White,draw.Texture?draw.Texture->Sampler:_DefaultSampler);
    if(key.Lit) {
        const auto lightUniform=MakeLitUniform(view,*draw.Object,*draw.Material,lights,draw.NormalTexture!=nullptr);
        pass.SetFragmentUniform(0,&lightUniform,sizeof(lightUniform));
        pass.SetFragmentUniform(1,environment,environmentSize);
        pass.SetFragmentUniform(2,&_ShadowUniform,sizeof(_ShadowUniform));
        pass.SetFragmentTextureSampler(2,_ShadowAtlas?_ShadowAtlas:_White,_ShadowSampler?_ShadowSampler:_DefaultSampler);
        pass.SetFragmentTextureSampler(1,draw.NormalTexture?draw.NormalTexture->Texture:_White,
            draw.NormalTexture?draw.NormalTexture->Sampler:_DefaultSampler);
    }
    pass.DrawIndexed(draw.Section.IndexCount,instanceCount,draw.Section.FirstIndex,0,0);
    ++_Statistics.DrawCalls;_Statistics.Triangles+=std::uint64_t(draw.Section.IndexCount/3)*instanceCount;
    return true;
}
}
