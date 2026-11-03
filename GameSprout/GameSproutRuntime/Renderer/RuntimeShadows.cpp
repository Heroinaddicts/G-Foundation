#include "RuntimeRenderer.h"
#include "RuntimeShadowsMath.h"
#include "RuntimeFrustum.h"
#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <cstring>

namespace GameSproutRuntime {
namespace {
struct ShadowPassScope {
    GRHI::ICommandBuffer& Command;
    GRHI::IRenderPass* Pass;
    ~ShadowPassScope(){if(Pass)Command.EndRenderPass(Pass);}
};
bool SameShadowCamera(const RenderCamera& a,const RenderCamera& b) {
    const auto& x=a.Projection;const auto& y=b.Projection;
    return !std::memcmp(a.View.Data,b.View.Data,sizeof(a.View.Data))&&
        x.Projection==y.Projection&&x.VerticalFovRadians==y.VerticalFovRadians&&
        x.OrthographicHeight==y.OrthographicHeight&&x.NearPlane==y.NearPlane&&
        x.FarPlane==y.FarPlane&&x.CullingMask==y.CullingMask;
}
Bounds WorldBounds(const Bounds& local,const Mat4& model) {
    const auto center=model*Vector4{local.Center.X,local.Center.Y,local.Center.Z,1};
    const auto e=local.Extents;
    return {{center.X,center.Y,center.Z},{
        std::fabs(model(0,0))*e.X+std::fabs(model(0,1))*e.Y+std::fabs(model(0,2))*e.Z,
        std::fabs(model(1,0))*e.X+std::fabs(model(1,1))*e.Y+std::fabs(model(1,2))*e.Z,
        std::fabs(model(2,0))*e.X+std::fabs(model(2,1))*e.Y+std::fabs(model(2,2))*e.Z}};
}
}

bool Renderer::DrawShadow(GRHI::IRenderPass& pass,const PreparedDraw& draw,const Mat4& lightViewProjection,
    GRHI::IBuffer* instances,std::uint32_t instanceCount) {
    const auto& data=draw.Mesh->Data;
    PipelineKey key;key.Shadow=true;key.Stride=data.VertexStride;key.Position=data.PositionOffset;
    key.Normal=data.NormalOffset;key.Color=data.ColorOffset;key.UV=data.UVOffset;
    key.DoubleSided=draw.Material->DoubleSided;key.Negative=draw.Object->Negative;key.Instanced=instances!=nullptr;
    if(draw.Object->Skinned){key.BoneIndices=data.BoneIndicesOffset;key.BoneWeights=data.BoneWeightsOffset;}
    auto* pipeline=Pipeline(key);if(!pipeline)return false;
    DrawUniform uniform;uniform.ViewProjection=lightViewProjection;
    if(!instances){uniform.Model=draw.Object->Model;uniform.Normal=draw.Object->Normal;}
    pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,draw.Mesh->Buffer,0);
    if(instances)pass.SetVertexBuffer(1,instances,0);
    pass.SetIndexBuffer(draw.Mesh->Buffer,GRHI::IndexFormat::UInt32,static_cast<std::size_t>(data.IndexOffset));
    pass.SetVertexUniform(0,&uniform,sizeof(uniform));
    if(draw.Object->Skinned) {
        SkinUniform skin;std::copy(draw.Object->SkinMatrices.begin(),draw.Object->SkinMatrices.end(),skin.Bones);
        pass.SetVertexUniform(1,&skin,sizeof(skin));
    }
    pass.DrawIndexed(draw.Section.IndexCount,instanceCount,draw.Section.FirstIndex,0,0);
    ++_Statistics.ShadowDrawCalls;return true;
}

bool Renderer::RenderShadows(GRHI::ICommandBuffer& command,const RenderView& view,float aspect) {
    static_assert(sizeof(ShadowUniform)==480,"Shadow fragment block must match HLSL packing");
    _ShadowLightIndex=static_cast<std::size_t>(-1);
    for(std::size_t index=0;index<_SceneLights.size();++index)
        if(_SceneLights[index].Shadows.Enabled&&_SceneLights[index].Shadows.Strength>0) {
            _ShadowLightIndex=index;break;
        }
    if(_ShadowLightIndex==static_cast<std::size_t>(-1)) {
        _ShadowUniform.Info={};_ShadowValid=false;return true;
    }
    const auto& light=_SceneLights[_ShadowLightIndex];const auto& settings=light.Shadows;
    if(settings.Distance<=view.Camera.Projection.NearPlane) {
        _ShadowUniform.Info={};_ShadowValid=false;return true;
    }
    const auto sceneID=view.Scene->GetInstanceID();
    const auto sceneRevision=static_cast<Scene*>(view.Scene)->GetRenderRevision();
    _Statistics.ShadowCascadeCount=settings.CascadeCount;
    if(_ShadowValid&&_ShadowSceneID==sceneID&&_ShadowSceneRevision==sceneRevision&&
        _ShadowAspect==aspect&&SameShadowCamera(_ShadowCamera,view.Camera))return true;
    _ShadowValid=false;_ShadowUniform.Info={};
    if(!_ShadowAtlas||_ShadowResolution!=settings.Resolution) {
        delete _ShadowAtlas;_ShadowAtlas=nullptr;_ShadowResolution=0;
        GRHI::TextureDesc description;description.Width=description.Height=settings.Resolution;
        description.Format=GRHI::TextureFormat::D32_Float;
        description.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::Sampled;
        _ShadowAtlas=_Device.CreateTexture(description);
        if(!_ShadowAtlas)return Fail(DeviceError("Create sampled directional shadow atlas"));
        _ShadowResolution=settings.Resolution;
    }
    if(!_ShadowSampler) {
        GRHI::SamplerDesc description;description.MinFilter=description.MagFilter=GRHI::FilterMode::Nearest;
        _ShadowSampler=_Device.CreateSampler(description);
        if(!_ShadowSampler)return Fail(DeviceError("Create directional shadow sampler"));
    }
    struct Candidate {PreparedDraw Draw;Bounds World;};
    std::vector<Candidate> candidates;
    std::vector<Bounds> casterBounds;
    const MaterialRenderData defaultMaterial;
    // The snapshot includes objects outside the color camera's frustum. Their
    // projections may cast a shadow into it; cull only in light space below.
    for(auto& object:_SceneObjects) {
        if(!(light.CullingMask&(std::uint32_t{1}<<object.Layer))||!object.Geometry)continue;
        auto* mesh=object.Geometry;
        Bounds local=mesh->Data.LocalBounds;
        if(mesh->CullBoundsKnown)local={mesh->CullMinimum*.5f+mesh->CullMaximum*.5f,mesh->CullMaximum*.5f-mesh->CullMinimum*.5f};
        if(object.Skinned)if(auto* node=_Runtime.QueryGameObject(object.ObjectID))
            if(auto* component=node->GetComponent<IMeshComponent>())local=component->GetLocalBounds();
        const Bounds world=WorldBounds(local,object.Model);
        bool included=false;
        for(const auto& section:mesh->Data.Sections) {
            if(!section.IndexCount)continue;
            const MaterialRenderData* material=&defaultMaterial;
            if(section.MaterialSlot<object.MaterialCache.size()) {
                auto* cached=object.MaterialCache[section.MaterialSlot];
                if(cached)material=&cached->Data;
                else if(!object.Materials[section.MaterialSlot].Guid.empty())continue;
            }
            // User programs require an explicit shadow-caster contract. Never
            // replace a potentially displaced/alpha-clipped shader with opaque geometry.
            if(!material->Shader.Guid.empty())continue;
            PreparedDraw draw;draw.Object=&object;draw.Mesh=mesh;draw.Section=section;draw.Material=material;
            draw.SectionOrdinal=static_cast<std::uint64_t>(&section-mesh->Data.Sections.data());
            candidates.push_back({draw,world});included=true;
        }
        if(included)casterBounds.push_back(world);
    }
    const std::uint32_t columns=settings.CascadeCount==1?1:2;
    const std::uint32_t tile=settings.Resolution/columns;
    RuntimeShadowMath::CascadeSet cascades;
    if(!RuntimeShadowMath::Build(view.Camera,aspect,light.TowardsLight,settings.Distance,settings.CascadeCount,
        tile,casterBounds.data(),casterBounds.size(),cascades))return Fail("Could not fit directional shadow cascades");
    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        if(a.Draw.Mesh->SortRank!=b.Draw.Mesh->SortRank)return a.Draw.Mesh->SortRank<b.Draw.Mesh->SortRank;
        if(a.Draw.Section.FirstIndex!=b.Draw.Section.FirstIndex)return a.Draw.Section.FirstIndex<b.Draw.Section.FirstIndex;
        if(a.Draw.Section.IndexCount!=b.Draw.Section.IndexCount)return a.Draw.Section.IndexCount<b.Draw.Section.IndexCount;
        if(a.Draw.Material->DoubleSided!=b.Draw.Material->DoubleSided)return a.Draw.Material->DoubleSided<b.Draw.Material->DoubleSided;
        return a.Draw.Object->Negative<b.Draw.Object->Negative;
    });
    GRHI::RenderPassDesc passDescription;passDescription.DepthTarget=_ShadowAtlas->GetDepthTargetView();
    passDescription.DepthLoad=GRHI::LoadOp::Clear;passDescription.DepthStore=GRHI::StoreOp::Store;passDescription.ClearDepth=1;
    auto* pass=command.BeginRenderPass(passDescription);
    if(!pass)return Fail(DeviceError("Begin depth-only directional shadow pass"));
    ShadowPassScope passScope{command,pass};
    bool success=true;
    ShadowUniform uniform;uniform.CameraView=view.Camera.View;
    const float maxDistance=cascades.Entries[settings.CascadeCount-1].FarDepth;
    uniform.Info={float(settings.CascadeCount),settings.Strength,maxDistance,maxDistance*.9f};
    for(std::uint32_t cascade=0;cascade<settings.CascadeCount&&success;++cascade) {
        const auto& fitted=cascades.Entries[cascade];
        const auto x=(cascade%columns)*tile,y=(cascade/columns)*tile;
        pass->SetViewport({float(x),float(y),float(tile),float(tile),0,1});
        uniform.WorldToShadow[cascade]=fitted.ViewProjection;
        if(cascade==0)uniform.Splits.X=fitted.FarDepth;
        else if(cascade==1)uniform.Splits.Y=fitted.FarDepth;
        else if(cascade==2)uniform.Splits.Z=fitted.FarDepth;
        else uniform.Splits.W=fitted.FarDepth;
        uniform.Bias[cascade]={settings.DepthBias*fitted.WorldUnitsPerTexel/fitted.DepthRange,
            settings.NormalBias*fitted.WorldUnitsPerTexel,settings.Soft?1.0f:0.0f,0};
        uniform.Atlas[cascade]={float(tile)/settings.Resolution,float(tile)/settings.Resolution,
            float(x)/settings.Resolution,float(y)/settings.Resolution};
        const RenderFrustum::Frustum frustum(fitted.ViewProjection);
        std::vector<PreparedDraw> draws;draws.reserve(candidates.size());
        for(const auto& candidate:candidates)
            if(frustum.Intersects(candidate.World.Min(),candidate.World.Max(),Mat4{}))draws.push_back(candidate.Draw);
        for(std::size_t begin=0;begin<draws.size()&&success;) {
            const auto& draw=draws[begin];std::size_t end=begin+1;
            if(view.EnableGPUInstancing&&draw.Material->EnableGPUInstancing&&!draw.Object->Skinned)while(end<draws.size()) {
                const auto& next=draws[end];
                if(next.Object->Skinned||!next.Material->EnableGPUInstancing||next.Mesh!=draw.Mesh||next.Section.FirstIndex!=draw.Section.FirstIndex||
                    next.Section.IndexCount!=draw.Section.IndexCount||next.Material->DoubleSided!=draw.Material->DoubleSided||
                    next.Object->Negative!=draw.Object->Negative)break;
                ++end;
            }
            for(std::uint32_t part=0;begin<end&&success;++part) {
                const auto count=std::min(MaxBatchInstances,end-begin);
                GRHI::IBuffer* instances=nullptr;
                if(count>1) {
                    InstanceBatchKey key;key.Pass=cascade+1;key.SceneID=sceneID;key.FirstObjectID=draws[begin].Object->ObjectID;
                    key.Mesh=draw.Object->Mesh;
                    key.GroupOrdinal=draws[begin].SectionOrdinal;key.FirstIndex=draw.Section.FirstIndex;key.IndexCount=draw.Section.IndexCount;
                    key.Part=part;key.Negative=draw.Object->Negative;
                    instances=InstanceBuffer(key,draws,begin,count);
                }
                if(instances)success=DrawShadow(*pass,draws[begin],fitted.ViewProjection,instances,static_cast<std::uint32_t>(count));
                else for(std::size_t index=begin;index<begin+count&&success;++index)
                    success=DrawShadow(*pass,draws[index],fitted.ViewProjection,nullptr,1);
                begin+=count;
            }
        }
    }
    command.EndRenderPass(pass);passScope.Pass=nullptr;
    if(!success)return false;
    _ShadowUniform=uniform;_ShadowCamera=view.Camera;_ShadowAspect=aspect;
    _ShadowSceneID=sceneID;_ShadowSceneRevision=sceneRevision;_ShadowRecorded=true;
    ++_Statistics.ShadowMapUpdates;return true;
}
}
