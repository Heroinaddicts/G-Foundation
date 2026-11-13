#include "RuntimeRenderer.h"
#include "RuntimeShadowsMath.h"
#include "GameSproutRuntime.h"
#include "Scene.h"
#include <algorithm>
#include <bit>
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
    std::size_t firstInstance,std::uint32_t instanceCount,const InstanceStream* instances) {
    const auto& data=draw.Mesh->Data;
    PipelineKey key;key.Shadow=true;key.Stride=data.VertexStride;key.Position=data.PositionOffset;
    key.Normal=data.NormalOffset;key.Color=data.ColorOffset;key.UV=data.UVOffset;
    key.DoubleSided=draw.Material->DoubleSided;key.Negative=draw.Object->Negative;key.Instanced=instances!=nullptr;
    if(draw.Object->Skinned){key.BoneIndices=data.BoneIndicesOffset;key.BoneWeights=data.BoneWeightsOffset;}
    auto* pipeline=Pipeline(key);if(!pipeline)return false;
    DrawUniform uniform;uniform.ViewProjection=lightViewProjection;
    if(!instances){uniform.Model=draw.Object->Model;uniform.Normal=draw.Object->Normal;}
    pass.SetPipeline(pipeline);pass.SetVertexBuffer(0,draw.Mesh->Buffer,0);
    if(instances) {
        pass.SetVertexBuffer(1,instances->Buffer,firstInstance*sizeof(std::uint32_t));
        pass.SetVertexStorageBuffer(0,_InstanceTransformsBuffer);
    }
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

void Renderer::SortShadowCandidates(std::vector<ShadowCandidate>& candidates) {
    std::stable_sort(candidates.begin(),candidates.end(),[](const ShadowCandidate& left,const ShadowCandidate& right) {
        const auto& a=left.Draw;
        const auto& b=right.Draw;
        if(a.Mesh->SortRank!=b.Mesh->SortRank)return a.Mesh->SortRank<b.Mesh->SortRank;
        if(a.Section.FirstIndex!=b.Section.FirstIndex)return a.Section.FirstIndex<b.Section.FirstIndex;
        if(a.Section.IndexCount!=b.Section.IndexCount)return a.Section.IndexCount<b.Section.IndexCount;
        if(a.Material->DoubleSided!=b.Material->DoubleSided)return a.Material->DoubleSided<b.Material->DoubleSided;
        return a.Object->Negative<b.Object->Negative;
    });
}

bool Renderer::DrawShadowAtlas(GRHI::ICommandBuffer& command,GRHI::ITexture& atlas,
    const RenderView& view,const RuntimeShadowMath::CascadeSet& cascades,
    std::uint32_t tile,std::uint32_t columns,const std::vector<ShadowCandidate>& candidates,
    InstanceStream& stream,std::uint32_t cascadeMask,const std::vector<ShadowRegion>* repair) {
    struct TileDraws {
        GRHI::Viewport Viewport;
        Mat4 Projection;
        std::vector<PreparedDraw> Draws;
        std::vector<InstanceDraw> Batches;
    };
    std::vector<TileDraws> tiles;
    const std::uint32_t allCascades=(std::uint32_t{1}<<cascades.Count)-1;
    cascadeMask&=allCascades;
    tiles.reserve(repair?repair->size():cascades.Count);
    stream.Indices.clear();
    for(std::uint32_t cascade=0;cascade<cascades.Count;++cascade) {
        if(!(cascadeMask&(std::uint32_t{1}<<cascade)))continue;
        const auto& projection=cascades.Entries[cascade].ViewProjection;
        const auto x=(cascade%columns)*tile,y=(cascade/columns)*tile;
        if(!repair) {
            auto& work=tiles.emplace_back(TileDraws{{float(x),float(y),float(tile),float(tile),0,1},projection,{},{}});
            work.Draws.reserve(candidates.size());
            for(const auto& candidate:candidates) {
                const auto bounds=ProjectShadowBounds(candidate.World,projection);
                if(bounds.Maximum[0]>=-1&&bounds.Minimum[0]<=1&&bounds.Maximum[1]>=-1&&bounds.Minimum[1]<=1&&
                   bounds.Maximum[2]>=0&&bounds.Minimum[2]<=1)work.Draws.push_back(candidate.Draw);
            }
            continue;
        }

        struct PatchBounds {double Left,Right,Bottom,Top;};
        std::vector<PatchBounds> patches;
        const auto firstPatch=tiles.size();
        for(const auto& region:*repair)if(region.Cascade==cascade) {
            const auto& viewport=region.Viewport;
            const double left=viewport.X-x,top=viewport.Y-y;
            patches.push_back({2*left/tile-1,2*(left+viewport.Width)/tile-1,
                1-2*(top+viewport.Height)/tile,1-2*top/tile});
            tiles.push_back({viewport,region.Projection,{},{}});
        }
        if(patches.empty())continue;
        // Project a caster once per cascade before testing its small repair rectangles.
        for(const auto& candidate:candidates) {
            const auto bounds=ProjectShadowBounds(candidate.World,projection);
            if(bounds.Maximum[2]<0||bounds.Minimum[2]>1)continue;
            for(std::size_t index=0;index<patches.size();++index) {
                const auto& patch=patches[index];
                if(!(bounds.Maximum[0]<patch.Left||bounds.Minimum[0]>patch.Right||
                     bounds.Maximum[1]<patch.Bottom||bounds.Minimum[1]>patch.Top))
                    tiles[firstPatch+index].Draws.push_back(candidate.Draw);
            }
        }
    }

    for(auto& work:tiles)
        PrepareInstanceBatches(work.Draws,view.EnableGPUInstancing,true,work.Batches,stream.Indices);
    if(!UploadInstanceStream(command,stream))return false;

    const bool preserveOtherTiles=repair||cascadeMask!=allCascades;
    GRHI::RenderPassDesc passDescription;
    passDescription.DepthTarget=atlas.GetDepthTargetView();
    passDescription.DepthLoad=preserveOtherTiles?GRHI::LoadOp::Load:GRHI::LoadOp::Clear;
    passDescription.DepthStore=GRHI::StoreOp::Store;
    passDescription.ClearDepth=1;
    auto* pass=command.BeginRenderPass(passDescription);
    if(!pass)return Fail(DeviceError("Begin depth-only directional shadow pass"));
    ShadowPassScope passScope{command,pass};
    for(const auto& work:tiles) {
        pass->SetViewport(work.Viewport);
        if(preserveOtherTiles&&!ClearShadowRegion(*pass))return false;
        for(const auto& batch:work.Batches)
            if(!DrawShadow(*pass,work.Draws[batch.First],work.Projection,batch.IndexOffset,batch.Count,
                batch.Count>1?&stream:nullptr))return false;
    }
    command.EndRenderPass(pass);
    passScope.Pass=nullptr;
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
    const std::uint32_t columns=settings.CascadeCount==1?1:2;
    const std::uint32_t tile=settings.Resolution/columns;
    const std::uint32_t allCascades=(std::uint32_t{1}<<settings.CascadeCount)-1;
    const float tileScale=float(tile)/settings.Resolution;
    const bool sameView=std::memcmp(_ShadowCamera.View.Data,view.Camera.View.Data,sizeof(view.Camera.View.Data))==0;
    const bool samePreview=_ShadowPreviewObjectID==previewID;
    const bool sameFitSettings=_ShadowValid&&_ShadowSceneID==sceneID&&_ShadowAspect==aspect&&
        _ShadowSettings==settings&&_ShadowLightMask==light.CullingMask&&
        SameVector(_ShadowDirection,light.TowardsLight)&&
        SameShadowProjection(_ShadowCamera.Projection,view.Camera.Projection);
    const bool navigationReuse=!sameView&&view.ReuseShadowsWhileNavigating;
    // Authored static maps have a small guard band. The current camera still
    // chooses the cascade, and the atlas is refit before coverage runs out.
    const float splits[]{_ShadowUniform.Splits.X,_ShadowUniform.Splits.Y,_ShadowUniform.Splits.Z,_ShadowUniform.Splits.W};
    std::uint32_t coveredCascades=0;
    if(!sameView&&!navigationReuse&&!_ShadowNavigationApproximate&&sameFitSettings&&
       settings.CacheStaticCasters&&!previewID&&!_ShadowPreviewObjectID) {
        coveredCascades=RuntimeShadowMath::CoveredCascades(view.Camera,aspect,_ShadowUniform.WorldToShadow,splits,
            settings.CascadeCount,tile,settings.NormalBias,settings.Soft);
    }
    const bool reuseFit=sameFitSettings&&(sameView||navigationReuse||coveredCascades==allCascades);
    const auto& samplingCamera=navigationReuse&&reuseFit?_ShadowCamera:view.Camera;
    const bool sameSampling=std::memcmp(_ShadowUniform.CameraView.Data,samplingCamera.View.Data,sizeof(samplingCamera.View.Data))==0;
    if(reuseFit&&sameSampling&&_ShadowSceneRevision==sceneRevision&&(samePreview||!previewID)) {
        // The submitted split still represents the exact committed pose.
        // Rebuild with the authored policy at the next actual invalidation.
        _Statistics.LayeredShadows=layered||_ShadowPreviewObjectID!=0;
        _ShadowNavigationApproximate=navigationReuse;
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
    std::unordered_set<std::uint64_t> staticTransformObjects;
    std::vector<DrawObject*> dynamicObjects;
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
    const bool unchangedStatic=settings.CacheStaticCasters&&sameFitSettings&&samePreview&&!_Statistics.SceneSnapshotRebuilds&&
        scene.GetRenderTransformChanges(_ShadowSceneRevision,moved)&&
        std::none_of(moved.begin(),moved.end(),[&](auto id){return _StaticShadowTransformObjects.contains(id);});
    const bool skipStatic=onlyPreviewMoved||unchangedStatic;
    const auto collectCaster=[&](DrawObject& object,bool dynamic) {
        if(!(light.CullingMask&(std::uint32_t{1}<<object.Layer))||!object.Geometry||
            (object.Skinned&&!object.SkinPoseReady))return;
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
            if(!dynamic) {
                primaryBounds.emplace_back(object.ObjectID,world);
                if(settings.CacheStaticCasters)
                    for(auto* node=_Runtime.QueryGameObject(object.ObjectID);node;node=node->Parent())
                        if(!staticTransformObjects.insert(node->GetInstanceID()).second)break;
            }
        }
    };
    // Off-camera objects can project into the camera frustum. Cache the list of
    // static casters and revisit only the dynamic subset while the scene is unchanged.
    if(unchangedStatic) {
        for(auto* object:_DynamicShadowObjects)collectCaster(*object,true);
    } else {
        for(auto& object:_SceneObjects) {
            // The moving preview subtree stays out of the primary atlas.
            const bool dynamic=layered&&(object.Skinned||previewSubtree.contains(object.ObjectID)||
                (settings.CacheStaticCasters&&!object.StaticShadowCaster));
            if(settings.CacheStaticCasters&&dynamic)dynamicObjects.push_back(&object);
            if(!skipStatic||dynamic)collectCaster(object,dynamic);
        }
        SortShadowCandidates(staticCasters);
    }

    const bool hadStaticLayer=_ShadowSettings.CacheStaticCasters||_ShadowPreviewObjectID!=0;
    const bool sameCasters=unchangedStatic||(layered&&hadStaticLayer&&_StaticShadowCasters==staticKeys);
    const bool reuseStatic=reuseFit&&(onlyPreviewMoved||sameCasters);
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
                if(!DrawShadowAtlas(command,*_ShadowAtlas,view,cascades,tile,columns,staticCasters,
                    _StaticShadowInstances,allCascades,&repair))return false;
                ++_Statistics.StaticShadowMapUpdates;
                _Statistics.StaticShadowDrawCalls=_Statistics.ShadowDrawCalls;
                std::uint32_t repairedMask=0;
                for(const auto& region:repair)repairedMask|=std::uint32_t{1}<<region.Cascade;
                _Statistics.StaticShadowCascadesUpdated=std::popcount(repairedMask);
            }
        }
    }
    const bool retainedPrimary=reuseStatic||repairStatic;
    const RenderCamera& fitCamera=retainedPrimary?samplingCamera:view.Camera;
    std::uint32_t refitMask=allCascades;
    if(!retainedPrimary&&sameCasters&&coveredCascades) {
        // If the regional clear is unavailable, a full clear/refit remains safe.
        const auto diagnostic=_Error;
        if(PrepareShadowRepair())refitMask&=~coveredCascades;
        _Error=diagnostic;
    }
    ShadowUniform uniform=(retainedPrimary||refitMask!=allCascades)?_ShadowUniform:ShadowUniform{};
    uniform.CameraView=fitCamera.View;
    if(!retainedPrimary) {
        RuntimeShadowMath::CascadeSet cascades;
        constexpr double StaticReceiverFitScale=1.125;
        const bool movingPerspective=settings.CacheStaticCasters&&fitCamera.Projection.Projection==CameraProjection::Perspective;
        const double fitScale=movingPerspective?StaticReceiverFitScale:1;
        const auto& fittingBounds=unchangedStatic?_StaticShadowBounds:staticBounds;
        const auto& fittingCasters=unchangedStatic?_StaticShadowCandidates:staticCasters;
        if(!RuntimeShadowMath::Build(fitCamera,aspect,light.TowardsLight,settings.Distance,
            settings.CascadeCount,tile,fittingBounds.data(),fittingBounds.size(),cascades,fitScale,nullptr,refitMask))
            return Fail("Could not fit directional shadow cascades");

        const float maxDistance=cascades.Entries[settings.CascadeCount-1].FarDepth;
        uniform.Info={float(settings.CascadeCount),settings.Strength,maxDistance,maxDistance*.9f};
        float splits[4]{};
        for(std::uint32_t index=0;index<cascades.Count;++index) {
            const auto& fitted=cascades.Entries[index];
            splits[index]=fitted.FarDepth;
            if(!(refitMask&(std::uint32_t{1}<<index)))continue;
            uniform.WorldToShadow[index]=fitted.ViewProjection;
            uniform.Bias[index]={settings.DepthBias*fitted.WorldUnitsPerTexel/fitted.DepthRange,
                settings.NormalBias*fitted.WorldUnitsPerTexel,settings.Soft?1.0f:0.0f,0};
            uniform.Atlas[index]={tileScale,tileScale,float(index%columns)*tileScale,float(index/columns)*tileScale};
        }
        uniform.Splits={splits[0],splits[1],splits[2],splits[3]};
        if(!DrawShadowAtlas(command,*_ShadowAtlas,view,cascades,tile,columns,fittingCasters,_StaticShadowInstances,refitMask))return false;
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
            _Statistics.StaticShadowCascadesUpdated=std::popcount(refitMask);
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
            settings.CascadeCount,tile,dynamicBounds.data(),dynamicBounds.size(),cascades,1,uniform.WorldToShadow))
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
        SortShadowCandidates(dynamicCasters);
        if(!DrawShadowAtlas(command,*_DynamicShadowAtlas,view,cascades,tile,columns,dynamicCasters,_DynamicShadowInstances))return false;
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
    _ShadowNavigationApproximate=retainedPrimary&&navigationReuse;
    if(!skipStatic) {
        _StaticShadowCasters=std::move(staticKeys);
        _PrimaryShadowCasterBounds=std::move(primaryBounds);
        _StaticShadowTransformObjects=std::move(staticTransformObjects);
        if(settings.CacheStaticCasters) {
            _StaticShadowCandidates=std::move(staticCasters);
            _StaticShadowBounds=std::move(staticBounds);
            _DynamicShadowObjects=std::move(dynamicObjects);
        } else {
            _StaticShadowCandidates.clear();
            _StaticShadowBounds.clear();
            _DynamicShadowObjects.clear();
        }
    }
    _ShadowRecorded=true;
    return true;
}
}
