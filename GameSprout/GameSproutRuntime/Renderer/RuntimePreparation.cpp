#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "Components/MeshComponent.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>

namespace GameSproutRuntime {
void Renderer::RefreshSkinPoses() {
    for(auto& skin:_SkinnedDraws) {
        auto& object=_SceneObjects[skin.ObjectIndex];
        if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
        auto* mesh=object.Geometry;
        auto* node=_Runtime.QueryGameObject(object.ObjectID);
        bool ready=mesh&&node&&SkinMatrices(*node,*mesh,_SkinScratch);
        auto* component=node?dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>()):nullptr;
        if(ready) {
            const bool samePose=object.SkinPoseReady&&object.SkinMatrices.size()==_SkinScratch.size()&&
                std::equal(_SkinScratch.begin(),_SkinScratch.end(),object.SkinMatrices.begin(),[](const Mat4& a,const Mat4& b) {
                    return !std::memcmp(a.Data,b.Data,sizeof(a.Data));
                });
            if(!samePose) {
                if(component&&component->GetMesh()==object.Mesh) {
                    skin.LocalBounds=mesh->Data.LocalBounds;
                    ready=PosedPositions(*mesh,_SkinScratch,_PosedPositionScratch);
                    if(ready&&!_PosedPositionScratch.empty()) {
                        Vector3 minimum=_PosedPositionScratch.front(),maximum=minimum;
                        for(const auto& point:_PosedPositionScratch) {
                            minimum={std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
                            maximum={std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
                        }
                        skin.LocalBounds={minimum*0.5f+maximum*0.5f,maximum*0.5f-minimum*0.5f};
                    }
                }
                if(ready)object.SkinMatrices.swap(_SkinScratch);
            }
            if(component&&component->GetMesh()==object.Mesh)component->_Bounds=skin.LocalBounds;
        } else if(!mesh&&component&&component->GetMesh()==object.Mesh)component->_Bounds={};
        if(object.SkinPoseReady!=ready) {
            _PreparedKnown=false;
            _OpaqueDrawOrderKnown=false;
        }
        object.SkinPoseReady=ready;
        if(!ready)object.SkinMatrices.clear();
    }
}

void Renderer::PrepareDraws(const RenderView& view,const Mat4& viewProjection) {
    // Draw packets point to live renderer-owned matrices. Refresh the skin
    // palette independently of visibility, resources and opaque draw order.
    RefreshSkinPoses();
    const auto& position=view.Camera.Position;
    const auto& current=view.Camera.Projection;const auto& previous=_PreparedCamera.Projection;
    if(_PreparedKnown&&
       !std::memcmp(_PreparedViewProjection.Data,viewProjection.Data,sizeof(viewProjection.Data))&&
       !std::memcmp(_PreparedCamera.View.Data,view.Camera.View.Data,sizeof(view.Camera.View.Data))&&
       current.Projection==previous.Projection&&current.VerticalFovRadians==previous.VerticalFovRadians&&
       current.OrthographicHeight==previous.OrthographicHeight&&current.NearPlane==previous.NearPlane&&
       current.FarPlane==previous.FarPlane&&current.CullingMask==previous.CullingMask&&
       position.X==_PreparedCamera.Position.X&&position.Y==_PreparedCamera.Position.Y&&position.Z==_PreparedCamera.Position.Z) {
        _Statistics.VisibleObjects+=_PreparedVisible;_Statistics.SkippedObjects+=_PreparedSkipped;
        Diagnostic(_PreparedDiagnostic);return;
    }
    // DrawObject/material addresses are stable until Extract or SyncRevision
    // invalidates this list. Camera movement rebuilds visibility and transparent sorting.
    _PreparedKnown=false;_PreparedDraws.clear();_ColorInstanceBatchesKnown=false;
    const auto visibleBefore=_Statistics.VisibleObjects,skippedBefore=_Statistics.SkippedObjects;
    const auto diagnosticBefore=_Error.size();
    const RenderFrustum::Frustum frustum(viewProjection);
    auto& objects=_SceneObjects;
    static const ResourceReference defaultMaterialReference;
    auto& visible=_VisibleDrawScratch;
    visible.clear();
    _VisibleTransparentSlots.clear();
    constexpr auto invisible=std::numeric_limits<std::size_t>::max();
    const bool rebuildOpaqueOrder=!_OpaqueDrawOrderKnown;
    std::vector<PreparedDraw> opaqueSections;
    _VisibleOpaqueSlots.clear();
    if(!rebuildOpaqueOrder)_VisibleOpaqueSlots.resize(_OpaqueDrawOrder.size(),invisible);
    std::size_t nextOpaqueSlot=0;
    bool hasOpaqueShader=false;
    for(auto& object:objects) {
        if(object.Skinned&&!object.SkinPoseReady) {++_Statistics.SkippedObjects;continue;}
        if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
        auto* mesh=object.Geometry;
        // 更新已加载资源的派生 bounds；不修改节点属性、主相机、Undo 或源场景快照。
        if(!object.Skinned&&!object.BoundsInitialized)if(auto* node=_Runtime.QueryGameObject(object.ObjectID)) {
            if(auto* component=dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>());
               component && component->GetMesh()==object.Mesh) {
                component->_Bounds=mesh?mesh->Data.LocalBounds:Bounds{};
                object.BoundsInitialized=true;
            }
        }
        if(!mesh) {++_Statistics.SkippedObjects;continue;}
        bool outside=false;
        if(!object.Skinned) {
            if(!mesh->CullBoundsKnown&&!mesh->Positions.empty()) {
                mesh->CullMinimum=mesh->CullMaximum=mesh->Positions.front();
                for(const auto& point:mesh->Positions) {
                    mesh->CullMinimum={std::min(mesh->CullMinimum.X,point.X),std::min(mesh->CullMinimum.Y,point.Y),std::min(mesh->CullMinimum.Z,point.Z)};
                    mesh->CullMaximum={std::max(mesh->CullMaximum.X,point.X),std::max(mesh->CullMaximum.Y,point.Y),std::max(mesh->CullMaximum.Z,point.Z)};
                }
                mesh->CullBoundsKnown=true;
            }
            if(mesh->CullBoundsKnown) {
                if(!object.CullBoxKnown) {
                    object.CullBox=RenderFrustum::Box(mesh->CullMinimum,mesh->CullMaximum,object.Model);
                    object.CullBoxKnown=true;
                }
                outside=!frustum.Intersects(object.CullBox);
            }
        }
        bool drawn=false,skipped=false;
        for(const auto& section:mesh->Data.Sections) {
            if(!section.IndexCount) continue;
            const MaterialRenderData* material=&_DefaultMaterial;
            Material* cachedMaterial=nullptr;
            const ResourceReference* materialReference=&defaultMaterialReference;
            if(section.MaterialSlot<object.Materials.size()) {
                const auto& reference=object.Materials[section.MaterialSlot];
                if(!reference.Guid.empty() || reference.LocalID) {
                    auto*& cached=object.MaterialCache[section.MaterialSlot];
                    if(!cached)cached=MaterialData(reference);
                    if(!cached) {skipped=true;continue;}
                    cachedMaterial=cached;material=&cached->Data;materialReference=&reference;
                }
            }
            const bool builtin=material->Shader.Guid.empty();
            const auto sectionOrdinal=static_cast<std::uint64_t>(&section-mesh->Data.Sections.data());
            auto opaqueSlot=invisible;
            if(builtin) {
                opaqueSlot=nextOpaqueSlot++;
                if(rebuildOpaqueOrder) {
                    opaqueSections.push_back({&object,mesh,section,material,materialReference,nullptr,nullptr,
                        cachedMaterial,nullptr,false,0,sectionOrdinal});
                    _VisibleOpaqueSlots.push_back(invisible);
                }
            }
            // User vertex shaders have no displacement-bounds contract yet;
            // never reject them using undeformed mesh bounds. Skinned meshes
            // are likewise retained regardless of their bind-pose position.
            if(outside&&cachedMaterial&&_TexturePrefetchEnabled&&!cachedMaterial->TexturePrefetchConsidered) {
                // A nearby object just outside the view can appear on the
                // next turn. Prefetch its material once per resource revision.
                constexpr float prefetchDistanceSquared=80.0f*80.0f;
                const Vector3 difference{object.Model(0,3)-position.X,object.Model(1,3)-position.Y,object.Model(2,3)-position.Z};
                if(difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z<=prefetchDistanceSquared)
                    PredictMaterialTextures(*cachedMaterial);
            }
            if(outside&&builtin){skipped=true;continue;}
            TextureGPU* texture=nullptr;TextureGPU* normalTexture=nullptr;
            ShaderGPU* shader=nullptr;bool transparent=false;
            if(!builtin) {
                if(object.Skinned){Diagnostic("User Shader currently supports static meshes; skinned Shader variants are not implemented");skipped=true;continue;}
                shader=Shader(material->Shader);
                if(!shader||!PrepareShaderMaterial(*cachedMaterial,*shader)||!EnsureShaderVertices(*mesh,object.Mesh)){skipped=true;continue;}
                transparent=shader->Data.Passes[0].Blend!=ShaderBlendMode::Opaque;
            }else if(mesh->Data.UVOffset>=0 && !material->BaseColorTexture.Guid.empty()) {
                bool pending=false;
                texture=MaterialTexture(*cachedMaterial,false,pending);
                if(!texture&&!pending) {skipped=true;continue;}
            }
            if(material->ShadingModel==MaterialShadingModel::Lit&&!material->NormalTexture.Guid.empty()) {
                if(mesh->Data.UVOffset<0){Diagnostic("Lit normal map requires mesh UV coordinates");skipped=true;continue;}
                bool pending=false;
                normalTexture=MaterialTexture(*cachedMaterial,true,pending);
                if(!normalTexture&&!pending){skipped=true;continue;}
                if(normalTexture&&normalTexture->SRGB){Diagnostic("Lit normal map must be imported in Linear color space");skipped=true;continue;}
            }
            float distance=0;
            if(transparent) {
                const Vector3 difference{object.Model(0,3)-position.X,object.Model(1,3)-position.Y,object.Model(2,3)-position.Z};
                distance=difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z;
                _VisibleTransparentSlots.push_back(visible.size());
            }else if(builtin) {
                _VisibleOpaqueSlots[opaqueSlot]=visible.size();
            }else hasOpaqueShader=true;
            visible.push_back({&object,mesh,section,material,materialReference,texture,normalTexture,cachedMaterial,
                shader,transparent,distance,sectionOrdinal});
            drawn=true;
        }
        if(drawn) ++_Statistics.VisibleObjects;
        if(skipped || !drawn) ++_Statistics.SkippedObjects;
    }
    RefreshDrawSortRanks();
    if(rebuildOpaqueOrder) {
        // Build from the scene's whole opaque section list. Filtering it by
        // visibility preserves state order and the original order of ties.
        _OpaqueDrawOrder.resize(opaqueSections.size());
        std::iota(_OpaqueDrawOrder.begin(),_OpaqueDrawOrder.end(),std::size_t{0});
        std::stable_sort(_OpaqueDrawOrder.begin(),_OpaqueDrawOrder.end(),[&](auto left,auto right) {
            const auto& a=opaqueSections[left];
            const auto& b=opaqueSections[right];
            if(a.Material->SortingPriority!=b.Material->SortingPriority)
                return a.Material->SortingPriority<b.Material->SortingPriority;
            return OpaqueDrawBefore(a,b);
        });
        _OpaqueDrawOrderKnown=true;
    }
    if(hasOpaqueShader) {
        // An opaque custom shader is an order barrier among draws of its
        // priority. Keep original traversal order on either side of it.
        SortPrepared(visible);
        _PreparedDraws.swap(visible);
    }else {
        _PreparedDraws.reserve(visible.size());
        for(const auto slot:_OpaqueDrawOrder) {
            const auto index=_VisibleOpaqueSlots[slot];
            if(index!=invisible)_PreparedDraws.push_back(visible[index]);
        }
        std::stable_sort(_VisibleTransparentSlots.begin(),_VisibleTransparentSlots.end(),[&](auto left,auto right) {
            return PriorityAndDepthBefore(visible[left],visible[right]);
        });
        for(const auto slot:_VisibleTransparentSlots)_PreparedDraws.push_back(visible[slot]);
    }
    _PreparedViewProjection=viewProjection;_PreparedCamera=view.Camera;
    _PreparedVisible=_Statistics.VisibleObjects-visibleBefore;_PreparedSkipped=_Statistics.SkippedObjects-skippedBefore;
    _PreparedDiagnostic=_Error.substr(diagnosticBefore);
    if(!_PreparedDiagnostic.empty()&&_PreparedDiagnostic.front()=='\n')_PreparedDiagnostic.erase(0,1);
    _PreparedKnown=true;
}
}
