#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
namespace {
constexpr auto InvalidTransformIndex=std::numeric_limits<std::size_t>::max();
}

bool Renderer::SetDrawTransform(DrawObject& object,const Mat4& model) {
    Mat4 inverse;
    if(!RuntimeMath::IsFinite(model)||!RuntimeMath::TryInverseAffine(model,inverse))return false;
    object.Model=model;
    object.CullBoxKnown=false;
    for(std::size_t row=0;row<3;++row)
        for(std::size_t column=0;column<3;++column)object.Normal(row,column)=inverse(column,row);
    const double determinant=double(model(0,0))*(double(model(1,1))*model(2,2)-double(model(1,2))*model(2,1))-
        double(model(0,1))*(double(model(1,0))*model(2,2)-double(model(1,2))*model(2,0))+
        double(model(0,2))*(double(model(1,0))*model(2,1)-double(model(1,1))*model(2,0));
    object.Negative=determinant<0;
    return true;
}

bool Renderer::UpdateSceneTransforms(IScene& scene,const std::vector<std::uint64_t>& roots,std::vector<DrawObject>& objects) {
    std::vector<IGameObject*> pending;
    std::unordered_set<std::uint64_t> visited;
    const RenderFrustum::Frustum preparedFrustum(_PreparedViewProjection);
    for(const auto id:roots) {
        if(visited.contains(id))continue;
        auto* root=_Runtime.QueryGameObject(id);
        if(!root||root->GetScene()!=&scene)return false;
        pending.push_back(root);
        while(!pending.empty()) {
            auto* node=pending.back();pending.pop_back();
            const auto objectID=node->GetInstanceID();
            if(!visited.insert(objectID).second)continue;
            const auto found=_SceneObjectIndices.find(objectID);
            if(found!=_SceneObjectIndices.end()) {
                // Crossing the singular-transform boundary changes extraction
                // membership and diagnostics. Rebuild only for that case.
                if(found->second==InvalidTransformIndex)return false;
                auto& draw=objects[found->second];
                const bool hadCullBox=_PreparedKnown&&draw.CullBoxKnown;
                const bool wasInFrustum=hadCullBox&&preparedFrustum.Intersects(draw.CullBox);
                const bool wasNegative=draw.Negative;
                if(!SetDrawTransform(draw,node->GetTransform()->GetWorldMatrix()))return false;
                if(draw.Negative!=wasNegative)_OpaqueDrawOrderKnown=false;
                if(!_InstanceTransformsDirty&&!draw.Skinned)_ChangedInstanceTransforms.push_back(draw.InstanceIndex);
                if(hadCullBox) {
                    draw.CullBox=RenderFrustum::Box(draw.Geometry->CullMinimum,draw.Geometry->CullMaximum,draw.Model);
                    draw.CullBoxKnown=true;
                }
                // Prepared draws refer to this DrawObject. A same-visibility
                // built-in draw can keep its sorted slot while its matrices change.
                if(_PreparedKnown&&(draw.Negative!=wasNegative||
                    (hadCullBox&&preparedFrustum.Intersects(draw.CullBox)!=wasInFrustum)||
                    std::any_of(draw.MaterialCache.begin(),draw.MaterialCache.end(),[](const Material* material){
                        return material&&!material->Data.Shader.Guid.empty();
                    })))_PreparedKnown=false;
            }
            for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));
        }
    }
    return true;
}

bool Renderer::Extract(const RenderView& view,std::vector<DrawObject>& objects) {
    const auto sceneID=view.Scene->GetInstanceID();
    const auto& scene=static_cast<const Scene&>(*view.Scene);
    const auto revision=scene.GetRenderRevision();
    const auto mask=view.Camera.Projection.CullingMask;
    if(_ObjectSnapshotKnown&&_ObjectSceneID==sceneID&&_ObjectCullingMask==mask) {
        std::vector<std::uint64_t> changes;
        if(_ObjectSceneRevision==revision||
            (scene.GetRenderTransformChanges(_ObjectSceneRevision,changes)&&UpdateSceneTransforms(*view.Scene,changes,objects))) {
            _ObjectSceneRevision=revision;
            _Statistics.SkippedObjects+=_ExtractionSkipped;
            Diagnostic(_ExtractionDiagnostic);
            return true;
        }
    }
    _ObjectSnapshotKnown=false;
    _PreparedKnown=false;
    _OpaqueDrawOrderKnown=false;
    _InstanceTransformsDirty=true;
    _ChangedInstanceTransforms.clear();
    _SkinnedDraws.clear();
    objects.clear();
    _SceneObjectIndices.clear();
    const auto skippedBefore=_Statistics.SkippedObjects;
    const auto diagnosticBefore=_Error.size();
    ++_Statistics.SceneSnapshotRebuilds;
    std::vector<IGameObject*> pending{view.Scene};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for(std::size_t index=object->GetChildCount();index>0;--index) pending.push_back(object->GetChild(index-1));
        auto* renderer=object->GetComponent<IMeshRendererComponent>();
        if(!renderer) continue;
        auto* mesh=object->GetComponent<IMeshComponent>();
        if(!mesh || !object->IsActiveInHierarchy() || !renderer->IsVisible() || !(view.Camera.Projection.CullingMask&(1u<<object->GetLayer()))) {
            ++_Statistics.SkippedObjects;continue;
        }
        DrawObject data;data.ObjectID=object->GetInstanceID();data.Layer=object->GetLayer();data.Name=object->GetName();
        data.Skinned=object->GetComponent<ISkinnedMeshRendererComponent>()!=nullptr;
        data.StaticShadowCaster=!data.Skinned&&
            (object->GetStaticFlags()&StaticFlags::ShadowCasterStatic)!=StaticFlags::None;
        if(!SetDrawTransform(data,object->GetTransform()->GetWorldMatrix())) {
            _SceneObjectIndices.emplace(data.ObjectID,InvalidTransformIndex);
            ++_Statistics.SkippedObjects;Diagnostic("Skipped singular/nonfinite mesh transform: "+data.Name);continue;
        }
        data.Mesh=mesh->GetMesh();
        if(objects.size()>=std::numeric_limits<std::uint32_t>::max())
            return Fail("Scene exceeds the renderer instance index range");
        data.InstanceIndex=static_cast<std::uint32_t>(objects.size());
        data.Materials.reserve(renderer->GetMaterialCount());
        for(std::size_t slot=0;slot<renderer->GetMaterialCount();++slot) data.Materials.push_back(renderer->GetMaterial(slot));
        data.MaterialCache.resize(data.Materials.size(),nullptr);
        _SceneObjectIndices.emplace(data.ObjectID,objects.size());
        if(data.Skinned)_SkinnedDraws.push_back({objects.size(),{}});
        objects.push_back(std::move(data));
    }
    _ObjectSceneID=sceneID;_ObjectSceneRevision=revision;_ObjectCullingMask=mask;
    _ExtractionSkipped=_Statistics.SkippedObjects-skippedBefore;
    _ExtractionDiagnostic=_Error.substr(diagnosticBefore);
    if(!_ExtractionDiagnostic.empty()&&_ExtractionDiagnostic.front()=='\n')_ExtractionDiagnostic.erase(0,1);
    _ObjectSnapshotKnown=true;
    return true;
}
}
