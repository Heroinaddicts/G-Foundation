#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
#include "Scene.h"
#include <cmath>

namespace GameSproutRuntime {
bool Renderer::Extract(const RenderView& view,std::vector<DrawObject>& objects) {
    const auto sceneID=view.Scene->GetInstanceID();
    const auto revision=static_cast<Scene*>(view.Scene)->GetRenderRevision();
    const auto mask=view.Camera.Projection.CullingMask;
    if(_ObjectSnapshotKnown&&_ObjectSceneID==sceneID&&_ObjectSceneRevision==revision&&_ObjectCullingMask==mask) {
        _Statistics.SkippedObjects+=_ExtractionSkipped;
        Diagnostic(_ExtractionDiagnostic);
        return true;
    }
    _ObjectSnapshotKnown=false;
    _PreparedKnown=false;_SnapshotHasSkin=false;
    objects.clear();
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
        DrawObject data;data.ObjectID=object->GetInstanceID();data.Layer=object->GetLayer();data.Name=object->GetName();data.Model=object->GetTransform()->GetWorldMatrix();
        data.Skinned=object->GetComponent<ISkinnedMeshRendererComponent>()!=nullptr;
        _SnapshotHasSkin|=data.Skinned;
        Mat4 inverse;
        if(!RuntimeMath::IsFinite(data.Model) || !RuntimeMath::TryInverseAffine(data.Model,inverse)) {
            ++_Statistics.SkippedObjects;Diagnostic("Skipped singular/nonfinite mesh transform: "+data.Name);continue;
        }
        for(std::size_t row=0;row<3;++row) for(std::size_t column=0;column<3;++column) data.Normal(row,column)=inverse(column,row);
        const double determinant=double(data.Model(0,0))*(double(data.Model(1,1))*data.Model(2,2)-double(data.Model(1,2))*data.Model(2,1))-
            double(data.Model(0,1))*(double(data.Model(1,0))*data.Model(2,2)-double(data.Model(1,2))*data.Model(2,0))+
            double(data.Model(0,2))*(double(data.Model(1,0))*data.Model(2,1)-double(data.Model(1,1))*data.Model(2,0));
        data.Negative=determinant<0;data.Mesh=mesh->GetMesh();
        data.Materials.reserve(renderer->GetMaterialCount());
        for(std::size_t slot=0;slot<renderer->GetMaterialCount();++slot) data.Materials.push_back(renderer->GetMaterial(slot));
        data.MaterialCache.resize(data.Materials.size(),nullptr);
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
