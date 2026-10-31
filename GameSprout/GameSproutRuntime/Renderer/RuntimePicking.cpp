#include "RuntimeRenderer.h"
#include "RuntimePickingMath.h"
#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include <exception>
#include <limits>

namespace {
struct QueryScope {
    bool& Busy;
    explicit QueryScope(bool& busy):Busy(busy) {Busy=true;}
    ~QueryScope() {Busy=false;}
};
}
namespace GameSproutRuntime {
bool Renderer::BeginQuery(IScene* scene) {
    if(_Rendering || _Runtime.IsExecuting()) return Fail("Renderer query cannot run inside a Runtime/provider callback or Render");
    _Error.clear();
    auto* state=_Runtime.Find(scene);
    if(!_Runtime.IsUsable(state) || state->SceneOwner!=scene) return Fail("Renderer query requires a live scene owned by this Runtime");
    return true;
}
bool Renderer::QueryObject(IScene* scene,std::uint64_t objectID,IGameObject*& object,MeshGPU*& mesh,Mat4& model) {
    object=nullptr;mesh=nullptr;
    auto* state=_Runtime.Find(objectID);
    if(!_Runtime.IsUsable(state) || state->SceneOwner!=scene) return false;
    object=_Runtime.QueryGameObject(objectID);
    auto* component=object->GetComponent<IMeshComponent>();
    if(!component) return false;
    model=object->GetTransform()->GetWorldMatrix();Mat4 inverse;
    if(!RuntimeMath::IsFinite(model) || !RuntimeMath::TryInverseAffine(model,inverse)) return false;
    mesh=Mesh(component->GetMesh());return mesh!=nullptr;
}
bool Renderer::Raycast(IScene* scene,const Ray& worldRay,SceneRaycastHit& hit,std::uint32_t cullingMask) {
    hit={};
    if(!BeginQuery(scene)) return false;
    Vector3 direction;
    if(!RuntimeMath::IsFinite(worldRay.Origin) || !worldRay.Direction.TryNormalize(direction)) return Fail("Raycast requires a finite origin and a nonzero finite direction");
    QueryScope query(_Rendering);RuntimeOperation operation(_Runtime);
    try {
        SyncRevision();
        if(!(cullingMask&1u)) return false;
        const PickingMath::Vector origin(worldRay.Origin),worldDirection(direction);
        double nearest=std::numeric_limits<double>::infinity();
        SceneRaycastHit candidate;
        std::vector<IGameObject*> pending{scene};
        while(!pending.empty()) {
            auto* object=pending.back();pending.pop_back();
            for(std::size_t index=object->GetChildCount();index>0;--index) pending.push_back(object->GetChild(index-1));
            auto* renderer=object->GetComponent<IMeshRendererComponent>();
            auto* component=object->GetComponent<IMeshComponent>();
            if(!renderer || !component || !object->IsActiveInHierarchy() || !renderer->IsVisible()) continue;
            const Mat4 model=object->GetTransform()->GetWorldMatrix();Mat4 inverse;
            if(!RuntimeMath::IsFinite(model) || !RuntimeMath::TryInverseAffine(model,inverse)) continue;
            auto* mesh=Mesh(component->GetMesh());if(!mesh) continue;
            std::vector<Vector3> positions;
            if(!PosedPositions(*object,*mesh,positions)) continue;
            const auto localOrigin=PickingMath::Transform(inverse,origin,1);
            // 不再次归一化：相交参数t继续以世界射线单位计量，非均匀缩放下仍可直接比较远近。
            const auto localDirection=PickingMath::Transform(inverse,worldDirection,0);
            for(const auto& section:mesh->Data.Sections) {
                // 与实际绘制一致：缺失材质/贴图的section并没有出现在画面中，不命中它。
                MaterialRenderData material;
                if(section.MaterialSlot<renderer->GetMaterialCount()) {
                    const auto reference=renderer->GetMaterial(section.MaterialSlot);
                    if(!reference.Guid.empty() || reference.LocalID) {
                        auto* cached=MaterialData(reference);if(!cached) continue;material=cached->Data;
                    }
                }
                if(mesh->Data.UVOffset>=0 && !material.BaseColorTexture.Guid.empty() && !Texture(material.BaseColorTexture)) continue;
                for(std::uint32_t offset=0;offset<section.IndexCount;offset+=3) {
                    const auto first=section.FirstIndex+offset;
                    const PickingMath::Vector a(positions[mesh->Indices[first]]),b(positions[mesh->Indices[first+1]]),c(positions[mesh->Indices[first+2]]);
                    double distance=0;
                    if(!PickingMath::Intersect(localOrigin,localDirection,a,b,c,distance)||distance>=nearest) continue;
                    const auto worldA=PickingMath::Transform(model,a,1),worldB=PickingMath::Transform(model,b,1),worldC=PickingMath::Transform(model,c,1);
                    auto normal=(worldB-worldA).Cross(worldC-worldA);
                    const double length=std::sqrt(normal.Dot(normal));
                    Vector3 position,worldNormal;
                    if(!std::isfinite(length)||length==0||!PickingMath::ToVector3(normal*(1.0/length),worldNormal)||
                       !PickingMath::ToVector3(origin+worldDirection*distance,position)) continue;
                    nearest=distance;candidate={object->GetInstanceID(),static_cast<float>(distance),position,worldNormal,first/3};
                }
            }
        }
        if(!candidate.ObjectID) return false;
        hit=candidate;return true;
    } catch(const std::exception& exception) {return Fail(std::string("Runtime Raycast: ")+exception.what());}
    catch(...) {return Fail("Unexpected Runtime Raycast failure");}
}
}
