#include "RuntimeRenderer.h"
#include "RuntimeMath.h"
#include <cmath>

namespace GameSproutRuntime {
bool Renderer::Extract(const RenderView& view,std::vector<DrawObject>& objects) {
    std::vector<IGameObject*> pending{view.Scene};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for(std::size_t index=object->GetChildCount();index>0;--index) pending.push_back(object->GetChild(index-1));
        auto* renderer=object->GetComponent<IMeshRendererComponent>();
        if(!renderer) continue;
        auto* mesh=object->GetComponent<IMeshComponent>();
        if(!mesh || !object->IsActiveInHierarchy() || !renderer->IsVisible() || !(view.Camera.Projection.CullingMask&1u)) {
            ++_Statistics.SkippedObjects;continue;
        }
        DrawObject data;data.ObjectID=object->GetInstanceID();data.Name=object->GetName();data.Model=object->GetTransform()->GetWorldMatrix();
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
        objects.push_back(std::move(data));
    }
    return true;
}
}
