#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "Components/MeshComponent.h"
#include <algorithm>
#include <cstring>

namespace GameSproutRuntime {
void Renderer::PrepareDraws(const RenderView& view,const Mat4& viewProjection) {
    const auto& position=view.Camera.Position;
    const auto& current=view.Camera.Projection;const auto& previous=_PreparedCamera.Projection;
    if(_PreparedKnown&&!_SnapshotHasSkin&&
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
    // invalidates this list. Camera movement rebuilds visibility and transparent
    // sorting; a skinned snapshot always rebuilds poses/bounds on every render.
    _PreparedKnown=false;_PreparedDraws.clear();
    const auto visibleBefore=_Statistics.VisibleObjects,skippedBefore=_Statistics.SkippedObjects;
    const auto diagnosticBefore=_Error.size();
    const RenderFrustum::Frustum frustum(viewProjection);
    auto& objects=_SceneObjects;
    static const ResourceReference defaultMaterialReference;
    auto& prepared=_PreparedDraws;
    for(auto& object:objects) {
        if(!object.Geometry)object.Geometry=Mesh(object.Mesh);
        auto* mesh=object.Geometry;
        if(mesh && object.Skinned) {
            auto* node=_Runtime.QueryGameObject(object.ObjectID);
            if(!node || !SkinMatrices(*node,*mesh,object.SkinMatrices)) {++_Statistics.SkippedObjects;continue;}
        }
        // 更新已加载资源的派生 bounds；不修改节点属性、主相机、Undo 或源场景快照。
        if(object.Skinned||!object.BoundsInitialized)if(auto* node=_Runtime.QueryGameObject(object.ObjectID)) {
            if(auto* component=dynamic_cast<MeshComponent*>(node->GetComponent<IMeshComponent>());
               component && component->GetMesh()==object.Mesh) {
                component->_Bounds=mesh?mesh->Data.LocalBounds:Bounds{};
                object.BoundsInitialized=true;
                if(mesh && object.Skinned) {
                    std::vector<Vector3> positions;
                    if(!PosedPositions(*node,*mesh,positions)) {++_Statistics.SkippedObjects;continue;}
                    if(!positions.empty()) {
                        Vector3 minimum=positions.front(),maximum=minimum;
                        for(const auto& point:positions) {
                            minimum={std::min(minimum.X,point.X),std::min(minimum.Y,point.Y),std::min(minimum.Z,point.Z)};
                            maximum={std::max(maximum.X,point.X),std::max(maximum.Y,point.Y),std::max(maximum.Z,point.Z)};
                        }
                        component->_Bounds={minimum*0.5f+maximum*0.5f,maximum*0.5f-minimum*0.5f};
                    }
                }
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
            // User vertex shaders have no displacement-bounds contract yet;
            // never reject them using undeformed mesh bounds. Skinned meshes
            // are likewise retained regardless of their bind-pose position.
            if(outside&&material->Shader.Guid.empty()){skipped=true;continue;}
            TextureGPU* texture=nullptr;TextureGPU* normalTexture=nullptr;
            ShaderGPU* shader=nullptr;bool transparent=false;
            if(!material->Shader.Guid.empty()) {
                if(object.Skinned){Diagnostic("User Shader currently supports static meshes; skinned Shader variants are not implemented");skipped=true;continue;}
                shader=Shader(material->Shader);
                if(!shader||!PrepareShaderMaterial(*cachedMaterial,*shader)||!EnsureShaderVertices(*mesh,object.Mesh)){skipped=true;continue;}
                transparent=shader->Data.Passes[0].Blend!=ShaderBlendMode::Opaque;
            }else if(mesh->Data.UVOffset>=0 && !material->BaseColorTexture.Guid.empty()) {
                texture=MaterialTexture(*cachedMaterial,false);
                if(!texture) {skipped=true;continue;}
            }
            if(material->ShadingModel==MaterialShadingModel::Lit&&!material->NormalTexture.Guid.empty()) {
                if(mesh->Data.UVOffset<0){Diagnostic("Lit normal map requires mesh UV coordinates");skipped=true;continue;}
                normalTexture=MaterialTexture(*cachedMaterial,true);
                if(!normalTexture){skipped=true;continue;}
                if(normalTexture->SRGB){Diagnostic("Lit normal map must be imported in Linear color space");skipped=true;continue;}
            }
            const Vector3 difference{object.Model(0,3)-view.Camera.Position.X,object.Model(1,3)-view.Camera.Position.Y,object.Model(2,3)-view.Camera.Position.Z};
            const float distance=difference.X*difference.X+difference.Y*difference.Y+difference.Z*difference.Z;
            prepared.push_back({&object,mesh,section,material,materialReference,texture,normalTexture,cachedMaterial,shader,transparent,distance,
                static_cast<std::uint64_t>(&section-mesh->Data.Sections.data())});drawn=true;
        }
        if(drawn) ++_Statistics.VisibleObjects;
        if(skipped || !drawn) ++_Statistics.SkippedObjects;
    }
    SortPrepared(prepared);
    _PreparedViewProjection=viewProjection;_PreparedCamera=view.Camera;
    _PreparedVisible=_Statistics.VisibleObjects-visibleBefore;_PreparedSkipped=_Statistics.SkippedObjects-skippedBefore;
    _PreparedDiagnostic=_Error.substr(diagnosticBefore);
    if(!_PreparedDiagnostic.empty()&&_PreparedDiagnostic.front()=='\n')_PreparedDiagnostic.erase(0,1);
    _PreparedKnown=!_SnapshotHasSkin;
}
}
