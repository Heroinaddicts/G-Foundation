#include "RuntimeRenderer.h"
#include "RuntimeShadowsMath.h"
#include "RuntimeFrustum.h"
#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace GameSproutRuntime {
namespace {
struct ShadowPassScope {
    GRHI::ICommandBuffer& Command;
    GRHI::IRenderPass* Pass;
    ~ShadowPassScope(){if(Pass)Command.EndRenderPass(Pass);}
};
bool SameShadowProjection(const CameraSettings& x,const CameraSettings& y) {
    return x.Projection==y.Projection&&x.VerticalFovRadians==y.VerticalFovRadians&&
        x.OrthographicHeight==y.OrthographicHeight&&x.NearPlane==y.NearPlane&&
        x.FarPlane==y.FarPlane&&x.CullingMask==y.CullingMask;
}
bool SameVector(Vector3 x,Vector3 y) {
    return x.X==y.X&&x.Y==y.Y&&x.Z==y.Z;
}
Bounds WorldBounds(const Bounds& local,const Mat4& model) {
    const auto center=model*Vector4{local.Center.X,local.Center.Y,local.Center.Z,1};
    const auto e=local.Extents;
    return {{center.X,center.Y,center.Z},{
        std::fabs(model(0,0))*e.X+std::fabs(model(0,1))*e.Y+std::fabs(model(0,2))*e.Z,
        std::fabs(model(1,0))*e.X+std::fabs(model(1,1))*e.Y+std::fabs(model(1,2))*e.Z,
        std::fabs(model(2,0))*e.X+std::fabs(model(2,1))*e.Y+std::fabs(model(2,2))*e.Z}};
}
struct ShadowClipBounds {
    double Minimum[3]{},Maximum[3]{};
};
ShadowClipBounds ProjectShadowBounds(const Bounds& bounds,const Mat4& projection) {
    const double center[]{bounds.Center.X,bounds.Center.Y,bounds.Center.Z};
    const double extent[]{bounds.Extents.X,bounds.Extents.Y,bounds.Extents.Z};
    ShadowClipBounds result;
    for(std::size_t row=0;row<3;++row) {
        double midpoint=projection(row,3),radius=0,magnitude=std::fabs(midpoint);
        for(std::size_t column=0;column<3;++column) {
            const double term=projection(row,column)*center[column];
            midpoint+=term;
            magnitude+=std::fabs(term);
            radius+=std::fabs(projection(row,column))*extent[column];
        }
        const double guard=32.0*std::numeric_limits<float>::epsilon()*(magnitude+radius+1);
        result.Minimum[row]=midpoint-radius-guard;
        result.Maximum[row]=midpoint+radius+guard;
    }
    return result;
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

bool Renderer::StaticShadowCasterKey::operator==(const StaticShadowCasterKey& other) const {
    return ObjectID==other.ObjectID&&Mesh==other.Mesh&&FirstIndex==other.FirstIndex&&
        IndexCount==other.IndexCount&&DoubleSided==other.DoubleSided&&
        std::memcmp(Model.Data,other.Model.Data,sizeof(Model.Data))==0&&
        SameVector(World.Center,other.World.Center)&&SameVector(World.Extents,other.World.Extents);
}

bool Renderer::DrawShadowAtlas(GRHI::ICommandBuffer& command,GRHI::ITexture& atlas,
    const RenderView& view,const RuntimeShadowMath::CascadeSet& cascades,
    std::uint32_t tile,std::uint32_t columns,std::vector<ShadowCandidate>& candidates,
    std::uint32_t passOffset,const std::vector<ShadowRegion>* repair) {
    const auto sortDraw=[](const PreparedDraw& a,const PreparedDraw& b) {
        if(a.Mesh->SortRank!=b.Mesh->SortRank)return a.Mesh->SortRank<b.Mesh->SortRank;
        if(a.Section.FirstIndex!=b.Section.FirstIndex)return a.Section.FirstIndex<b.Section.FirstIndex;
        if(a.Section.IndexCount!=b.Section.IndexCount)return a.Section.IndexCount<b.Section.IndexCount;
        if(a.Material->DoubleSided!=b.Material->DoubleSided)return a.Material->DoubleSided<b.Material->DoubleSided;
        return a.Object->Negative<b.Object->Negative;
    };
    if(!repair)std::stable_sort(candidates.begin(),candidates.end(),[&](const ShadowCandidate& a,const ShadowCandidate& b) {
        return sortDraw(a.Draw,b.Draw);
    });
    GRHI::RenderPassDesc passDescription;passDescription.DepthTarget=atlas.GetDepthTargetView();
    passDescription.DepthLoad=repair?GRHI::LoadOp::Load:GRHI::LoadOp::Clear;
    passDescription.DepthStore=GRHI::StoreOp::Store;passDescription.ClearDepth=1;
    auto* pass=command.BeginRenderPass(passDescription);
    if(!pass)return Fail(DeviceError("Begin depth-only directional shadow pass"));
    ShadowPassScope passScope{command,pass};
    const auto drawBatch=[&](const std::vector<PreparedDraw>& draws,const Mat4& projection,std::uint32_t batchPass) {
        bool success=true;
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
                    InstanceBatchKey key;key.Pass=batchPass;key.SceneID=view.Scene->GetInstanceID();key.FirstObjectID=draws[begin].Object->ObjectID;
                    key.Mesh=draw.Object->Mesh;
                    key.GroupOrdinal=draws[begin].SectionOrdinal;key.FirstIndex=draw.Section.FirstIndex;key.IndexCount=draw.Section.IndexCount;
                    key.Part=part;key.Negative=draw.Object->Negative;
                    instances=InstanceBuffer(key,draws,begin,count);
                }
                if(instances)success=DrawShadow(*pass,draws[begin],projection,instances,static_cast<std::uint32_t>(count));
                else for(std::size_t index=begin;index<begin+count&&success;++index)
                    success=DrawShadow(*pass,draws[index],projection,nullptr,1);
                begin+=count;
            }
        }
        return success;
    };
    std::vector<PreparedDraw> draws;
    if(!repair)draws.reserve(candidates.size());
    for(std::uint32_t cascade=0;cascade<cascades.Count;++cascade) {
        const auto& projection=cascades.Entries[cascade].ViewProjection;
        const auto x=(cascade%columns)*tile,y=(cascade/columns)*tile;
        if(!repair) {
            pass->SetViewport({float(x),float(y),float(tile),float(tile),0,1});
            draws.clear();
            const RenderFrustum::Frustum frustum(projection);
            for(const auto& candidate:candidates)
                if(frustum.Intersects(candidate.World.Min(),candidate.World.Max(),Mat4{}))draws.push_back(candidate.Draw);
            if(!drawBatch(draws,projection,passOffset+cascade))return false;
            continue;
        }

        struct PatchDraws {
            const ShadowRegion& Region;
            double Left,Right,Bottom,Top;
            std::vector<PreparedDraw> Draws;
        };
        std::vector<PatchDraws> patches;
        for(const auto& region:*repair)if(region.Cascade==cascade) {
            const auto& viewport=region.Viewport;
            const double left=viewport.X-x,top=viewport.Y-y;
            patches.push_back({region,2*left/tile-1,2*(left+viewport.Width)/tile-1,
                1-2*(top+viewport.Height)/tile,1-2*top/tile,{}});
        }
        if(patches.empty())continue;
        // Project a caster once per cascade, then test its ranges against the
        // small, disjoint repair rectangles before sorting any draws.
        for(const auto& candidate:candidates) {
            const auto bounds=ProjectShadowBounds(candidate.World,projection);
            if(bounds.Maximum[2]<0||bounds.Minimum[2]>1)continue;
            for(auto& patch:patches)
                if(!(bounds.Maximum[0]<patch.Left||bounds.Minimum[0]>patch.Right||
                     bounds.Maximum[1]<patch.Bottom||bounds.Minimum[1]>patch.Top))
                    patch.Draws.push_back(candidate.Draw);
        }
        for(auto& patch:patches) {
            pass->SetViewport(patch.Region.Viewport);
            if(!ClearShadowRegion(*pass))return false;
            std::stable_sort(patch.Draws.begin(),patch.Draws.end(),sortDraw);
            const auto batchPass=9+static_cast<std::uint32_t>(&patch.Region-repair->data());
            if(!drawBatch(patch.Draws,patch.Region.Projection,batchPass))return false;
        }
    }
    command.EndRenderPass(pass);passScope.Pass=nullptr;
    ++_Statistics.ShadowMapUpdates;
    return true;
}

bool Renderer::RenderShadows(GRHI::ICommandBuffer& command,const RenderView& view,float aspect) {
    _ShadowLightIndex=static_cast<std::size_t>(-1);
    for(std::size_t index=0;index<_SceneLights.size();++index) {
        if(_SceneLights[index].Shadows.Enabled&&_SceneLights[index].Shadows.Strength>0) {
            _ShadowLightIndex=index;
            break;
        }
    }
    if(_ShadowLightIndex==static_cast<std::size_t>(-1)||
        _SceneLights[_ShadowLightIndex].Shadows.Distance<=view.Camera.Projection.NearPlane) {
        _ShadowUniform.Info={};
        _ShadowValid=false;
        return true;
    }

    const auto& light=_SceneLights[_ShadowLightIndex];
    const auto& settings=light.Shadows;
    auto* preview=view.TransformPreviewObjectID?_Runtime.QueryGameObject(view.TransformPreviewObjectID):nullptr;
    if(preview&&(preview==view.Scene||preview->GetScene()!=view.Scene))preview=nullptr;
    const auto previewID=preview?preview->GetInstanceID():0;
    const bool layered=settings.CacheStaticCasters||previewID!=0;
    const auto sceneID=view.Scene->GetInstanceID();
    const auto& scene=static_cast<const Scene&>(*view.Scene);
    const auto sceneRevision=scene.GetRenderRevision();
    _Statistics.ShadowCascadeCount=settings.CascadeCount;
    _Statistics.LayeredShadows=layered;
    const bool sameView=std::memcmp(_ShadowCamera.View.Data,view.Camera.View.Data,sizeof(view.Camera.View.Data))==0;
    // Both atlases and the camera used to select a cascade must use the same XY fit.
    const bool reuseFit=_ShadowValid&&_ShadowSceneID==sceneID&&_ShadowAspect==aspect&&
        _ShadowSettings==settings&&_ShadowLightMask==light.CullingMask&&
        SameVector(_ShadowDirection,light.TowardsLight)&&
        SameShadowProjection(_ShadowCamera.Projection,view.Camera.Projection)&&
        (sameView||view.ReuseShadowsWhileNavigating);
    const bool samePreview=_ShadowPreviewObjectID==previewID;
    if(reuseFit&&_ShadowSceneRevision==sceneRevision&&(samePreview||!previewID)) {
        // The submitted split still represents the exact committed pose.
        // Rebuild with the authored policy at the next actual invalidation.
        _Statistics.LayeredShadows=layered||_ShadowPreviewObjectID!=0;
        return true;
    }

    _ShadowValid=false;
    if(_ShadowResolution!=settings.Resolution) {
        delete _ShadowAtlas;
        _ShadowAtlas=nullptr;
        delete _DynamicShadowAtlas;
        _DynamicShadowAtlas=nullptr;
        _ShadowResolution=settings.Resolution;
    }
    if(!layered&&_DynamicShadowAtlas) {
        delete _DynamicShadowAtlas;
        _DynamicShadowAtlas=nullptr;
    }
    GRHI::TextureDesc description;
    description.Width=description.Height=settings.Resolution;
    description.Format=GRHI::TextureFormat::D32_Float;
    description.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::Sampled;
    if(!_ShadowAtlas) {
        _ShadowAtlas=_Device.CreateTexture(description);
        if(!_ShadowAtlas)return Fail(DeviceError("Create sampled directional shadow atlas"));
    }
    if(!_ShadowSampler) {
        GRHI::SamplerDesc sampler;
        sampler.MinFilter=sampler.MagFilter=GRHI::FilterMode::Nearest;
        _ShadowSampler=_Device.CreateSampler(sampler);
        if(!_ShadowSampler)return Fail(DeviceError("Create directional shadow sampler"));
    }

    std::vector<ShadowCandidate> staticCasters,dynamicCasters;
    std::vector<Bounds> staticBounds,dynamicBounds;
    std::vector<StaticShadowCasterKey> staticKeys;
    std::vector<std::pair<std::uint64_t,Bounds>> primaryBounds;
    std::unordered_set<std::uint64_t> previewSubtree;
    if(preview) {
        std::vector<IGameObject*> pending{preview};
        while(!pending.empty()) {
            auto* object=pending.back();pending.pop_back();
            previewSubtree.insert(object->GetInstanceID());
            for(std::size_t index=0;index<object->GetChildCount();++index)pending.push_back(object->GetChild(index));
        }
    }
    std::vector<std::uint64_t> moved;
    // The journal can prove that no cached caster moved during this drag.
    // Structural/material changes and edits outside the subtree still compare keys.
    const bool previewChangesOnly=preview&&reuseFit&&scene.GetRenderTransformChanges(_ShadowSceneRevision,moved)&&
        std::all_of(moved.begin(),moved.end(),[&](auto id){return previewSubtree.contains(id);});
    const bool onlyPreviewMoved=previewChangesOnly&&samePreview;
    // Off-camera objects can project into the camera frustum. Cull in light space.
    for(auto& object:_SceneObjects) {
        if(!(light.CullingMask&(std::uint32_t{1}<<object.Layer))||!object.Geometry)continue;
        // During an edit the moving subtree must stay out of the cached atlas,
        // even if authored static. With unified shadows, the unchanged rigid
        // surroundings can still be cached for the duration of the gesture.
        const bool dynamic=layered&&(object.Skinned||previewSubtree.contains(object.ObjectID)||
            (settings.CacheStaticCasters&&!object.StaticShadowCaster));
        if(onlyPreviewMoved&&!dynamic)continue;
        auto* mesh=object.Geometry;
        Bounds local=mesh->Data.LocalBounds;
        if(mesh->CullBoundsKnown)local={mesh->CullMinimum*.5f+mesh->CullMaximum*.5f,mesh->CullMaximum*.5f-mesh->CullMinimum*.5f};
        if(object.Skinned)if(auto* node=_Runtime.QueryGameObject(object.ObjectID))
            if(auto* component=node->GetComponent<IMeshComponent>())local=component->GetLocalBounds();
        const Bounds world=WorldBounds(local,object.Model);
        auto& casters=dynamic?dynamicCasters:staticCasters;
        auto& bounds=dynamic?dynamicBounds:staticBounds;
        const auto previousCount=casters.size();
        for(const auto& section:mesh->Data.Sections) {
            if(!section.IndexCount)continue;
            const MaterialRenderData* material=&_DefaultMaterial;
            if(section.MaterialSlot<object.MaterialCache.size()) {
                auto* cached=object.MaterialCache[section.MaterialSlot];
                if(cached)material=&cached->Data;
                else if(!object.Materials[section.MaterialSlot].Guid.empty())continue;
            }
            // User programs require an explicit shadow-caster contract.
            if(!material->Shader.Guid.empty())continue;
            PreparedDraw draw;
            draw.Object=&object;draw.Mesh=mesh;draw.Section=section;draw.Material=material;
            draw.SectionOrdinal=static_cast<std::uint64_t>(&section-mesh->Data.Sections.data());
            casters.push_back({draw,world});
            if(layered&&!dynamic)staticKeys.push_back({object.ObjectID,mesh,object.Model,world,
                section.FirstIndex,section.IndexCount,material->DoubleSided});
        }
        if(casters.size()!=previousCount) {
            bounds.push_back(world);
            if(!dynamic)primaryBounds.emplace_back(object.ObjectID,world);
        }
    }

    const bool hadStaticLayer=_ShadowSettings.CacheStaticCasters||_ShadowPreviewObjectID!=0;
    const bool reuseStatic=onlyPreviewMoved||(layered&&reuseFit&&hadStaticLayer&&_StaticShadowCasters==staticKeys);
    const std::uint32_t columns=settings.CascadeCount==1?1:2;
    const std::uint32_t tile=settings.Resolution/columns;
    const float tileScale=float(tile)/settings.Resolution;
    std::vector<ShadowRegion> repair;
    bool repairStatic=false;
    if(!reuseStatic&&previewChangesOnly&&!samePreview) {
        // A change of the dragged subtree affects the primary atlas only where
        // a caster entered or left it. Retain the previous depth normalization.
        std::unordered_map<std::uint64_t,Bounds> previous;
        previous.reserve(_PrimaryShadowCasterBounds.size());
        for(const auto& [id,bounds]:_PrimaryShadowCasterBounds)previous.emplace(id,bounds);
        std::vector<Bounds> removed,added;
        for(const auto& [id,bounds]:primaryBounds) {
            const auto found=previous.find(id);
            if(found==previous.end())added.push_back(bounds);
            else {
                if(!SameVector(found->second.Center,bounds.Center)||!SameVector(found->second.Extents,bounds.Extents)) {
                    removed.push_back(found->second);
                    added.push_back(bounds);
                }
                previous.erase(found);
            }
        }
        for(const auto& entry:previous)removed.push_back(entry.second);
        repairStatic=BuildShadowRepairRegions(_ShadowUniform.WorldToShadow,removed,added,tile,columns,settings.CascadeCount,repair);
        if(repairStatic&&!repair.empty()) {
            const auto diagnostic=_Error;
            repairStatic=PrepareShadowRepair()!=nullptr;
            _Error=diagnostic;
            if(repairStatic) {
                RuntimeShadowMath::CascadeSet cascades;
                cascades.Count=settings.CascadeCount;
                for(std::uint32_t index=0;index<cascades.Count;++index)
                    cascades.Entries[index].ViewProjection=_ShadowUniform.WorldToShadow[index];
                if(!DrawShadowAtlas(command,*_ShadowAtlas,view,cascades,tile,columns,staticCasters,1,&repair))return false;
                ++_Statistics.StaticShadowMapUpdates;
                _Statistics.StaticShadowDrawCalls=_Statistics.ShadowDrawCalls;
            }
        }
    }
    const bool retainedPrimary=reuseStatic||repairStatic;
    const RenderCamera& fitCamera=retainedPrimary?_ShadowCamera:view.Camera;
    ShadowUniform uniform=retainedPrimary?_ShadowUniform:ShadowUniform{};
    if(!retainedPrimary) {
        RuntimeShadowMath::CascadeSet cascades;
        if(!RuntimeShadowMath::Build(fitCamera,aspect,light.TowardsLight,settings.Distance,
            settings.CascadeCount,tile,staticBounds.data(),staticBounds.size(),cascades))
            return Fail("Could not fit directional shadow cascades");

        uniform.CameraView=fitCamera.View;
        const float maxDistance=cascades.Entries[settings.CascadeCount-1].FarDepth;
        uniform.Info={float(settings.CascadeCount),settings.Strength,maxDistance,maxDistance*.9f};
        float splits[4]{};
        for(std::uint32_t index=0;index<cascades.Count;++index) {
            const auto& fitted=cascades.Entries[index];
            uniform.WorldToShadow[index]=fitted.ViewProjection;
            splits[index]=fitted.FarDepth;
            uniform.Bias[index]={settings.DepthBias*fitted.WorldUnitsPerTexel/fitted.DepthRange,
                settings.NormalBias*fitted.WorldUnitsPerTexel,settings.Soft?1.0f:0.0f,0};
            uniform.Atlas[index]={tileScale,tileScale,float(index%columns)*tileScale,float(index/columns)*tileScale};
        }
        uniform.Splits={splits[0],splits[1],splits[2],splits[3]};
        if(!DrawShadowAtlas(command,*_ShadowAtlas,view,cascades,tile,columns,staticCasters,1))return false;
        // Warm the small clear pipeline outside a drag. Partial repair remains
        // optional if this backend cannot allocate its resources.
        if(!preview) {
            const auto diagnostic=_Error;
            PrepareShadowRepair();
            _Error=diagnostic;
        }
        if(layered) {
            ++_Statistics.StaticShadowMapUpdates;
            _Statistics.StaticShadowDrawCalls=_Statistics.ShadowDrawCalls;
        }
    }

    // If the last dynamic caster disappears, ignore its old atlas contents.
    uniform.Bias[0].W=0;
    if(!dynamicCasters.empty()) {
        if(!_DynamicShadowAtlas) {
            _DynamicShadowAtlas=_Device.CreateTexture(description);
            if(!_DynamicShadowAtlas)return Fail(DeviceError("Create dynamic directional shadow atlas"));
        }
        RuntimeShadowMath::CascadeSet cascades;
        if(!RuntimeShadowMath::Build(fitCamera,aspect,light.TowardsLight,settings.Distance,
            settings.CascadeCount,tile,dynamicBounds.data(),dynamicBounds.size(),cascades))
            return Fail("Could not fit dynamic directional shadow cascades");
        float depthBias[4]{};
        for(std::uint32_t index=0;index<cascades.Count;++index) {
            const auto& fitted=cascades.Entries[index];
            const auto& matrix=fitted.ViewProjection;
            uniform.DynamicDepth[index]={matrix(2,0),matrix(2,1),matrix(2,2),matrix(2,3)};
            depthBias[index]=settings.DepthBias*fitted.WorldUnitsPerTexel/fitted.DepthRange;
        }
        uniform.DynamicDepthBias={depthBias[0],depthBias[1],depthBias[2],depthBias[3]};
        uniform.Bias[0].W=1;
        if(!DrawShadowAtlas(command,*_DynamicShadowAtlas,view,cascades,tile,columns,dynamicCasters,5))return false;
        ++_Statistics.DynamicShadowMapUpdates;
        _Statistics.DynamicShadowDrawCalls=_Statistics.ShadowDrawCalls-_Statistics.StaticShadowDrawCalls;
    }

    _ShadowUniform=uniform;
    if(!retainedPrimary)_ShadowCamera=view.Camera;
    _ShadowSettings=settings;
    _ShadowDirection=light.TowardsLight;
    _ShadowLightMask=light.CullingMask;
    _ShadowPreviewObjectID=previewID;
    _ShadowAspect=aspect;
    _ShadowSceneID=sceneID;
    _ShadowSceneRevision=sceneRevision;
    if(!onlyPreviewMoved) {
        _StaticShadowCasters=std::move(staticKeys);
        _PrimaryShadowCasterBounds=std::move(primaryBounds);
    }
    _ShadowRecorded=true;
    return true;
}
}
