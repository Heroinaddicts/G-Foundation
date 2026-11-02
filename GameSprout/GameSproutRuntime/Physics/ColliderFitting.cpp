#include "PhysicsComponents.h"
#include "Api/RuntimeResourceApi.h"
#include "Api/RuntimeBuiltinResources.h"
#include "Api/Math/TransformMath.h"
#include "GameObject.h"
#include "Resources/BuiltinMeshes.h"
#include <exception>

namespace GameSproutRuntime {
bool CalculateColliderFit(const IColliderComponent& collider,ColliderFitResult& output,std::string& error) {
    output={};error.clear();
    if(const auto* meshCollider=dynamic_cast<const IMeshColliderComponent*>(&collider))
        output.Mesh.Convex=meshCollider->GetShape().Convex;
    auto* owner=collider.GetGameObject();
    auto* access=dynamic_cast<ObjectAccess*>(owner);
    if(!owner||!access){error="Collider owner is unavailable";return false;}
    auto& runtime=access->State().Owner;
    RuntimeOperation operation(runtime);
    // A SkinnedMeshRenderer augments Mesh on the same object. Its authored,
    // prepared bounds are used; fitting never samples or skins an animation.
    auto* mesh=owner->GetComponent<IMeshComponent>();
    if(!mesh)return true;
    const auto reference=mesh->GetMesh();
    output.Mesh.Mesh=reference;
    if(reference.Guid.empty()||!reference.LocalID)return true;
    try {
        MeshRenderData data;
        if(IsBuiltinResource(reference)) {
            if(!BuiltinMeshes::Load(reference,data,error))return false;
        }else {
            auto* provider=runtime.GetResourceProvider();
            if(!provider){error="Collider fitting requires prepared mesh resources";return false;}
            if(!provider->LoadMesh(reference,data,error))return false;
        }
        const auto bounds=data.LocalBounds;
        if(!RuntimeMath::IsFinite(bounds.Center)||!RuntimeMath::IsFinite(bounds.Extents)
            ||bounds.Extents.X<0||bounds.Extents.Y<0||bounds.Extents.Z<0) {
            error="Prepared mesh has invalid local bounds";return false;
        }
        if(bounds.Extents.X==0&&bounds.Extents.Y==0&&bounds.Extents.Z==0)return true;
        ColliderFitResult result=output;result.HasBounds=true;
        result.Center=dynamic_cast<const IMeshColliderComponent*>(&collider)?Vector3{}:bounds.Center;
        // The component's optional local rotation is independent of Transform.
        // Fit its axes to the same local AABB instead of scaling a world AABB.
        const auto rotation=collider.GetColliderSettings().Rotation;
        const auto inverse=RuntimeMath::ComposeTRS({},Quaternion{-rotation.X,-rotation.Y,-rotation.Z,rotation.W},{1,1,1});
        Vector3 extent;
        for(unsigned row=0;row<3;++row) {
            const float value=std::fabs(inverse(row,0))*bounds.Extents.X+
                std::fabs(inverse(row,1))*bounds.Extents.Y+std::fabs(inverse(row,2))*bounds.Extents.Z;
            if(row==0)extent.X=value;else if(row==1)extent.Y=value;else extent.Z=value;
        }
        constexpr float minimum=0.0001f;
        result.Box.Size={std::max(extent.X*2,minimum),std::max(extent.Y*2,minimum),std::max(extent.Z*2,minimum)};
        // A sphere has no orientation; changing the component rotation must
        // not change its fitted radius.
        result.Sphere.Radius=std::max({bounds.Extents.X,bounds.Extents.Y,bounds.Extents.Z,minimum});
        result.Capsule.Axis=PhysicsAxis::Y;
        result.Capsule.Radius=std::max({extent.X,extent.Z,minimum});
        result.Capsule.Height=std::max(extent.Y*2,result.Capsule.Radius*2);
        if(!RuntimeMath::IsFinite(result.Box.Size)||!std::isfinite(result.Sphere.Radius)
            ||!std::isfinite(result.Capsule.Radius)||!std::isfinite(result.Capsule.Height)) {
            error="Fitted collider dimensions are not representable";return false;
        }
        output=std::move(result);return true;
    }catch(const std::exception& failure){error=std::string("Collider fitting failed: ")+failure.what();return false;}
    catch(...){error="Collider fitting provider failed";return false;}
}
void InitializeColliderFit(PhysicsColliderState& state) {
    // Defaults are established once, before registration/OnCreate. A scene or
    // Prefab loader writes serialized properties afterwards; no later fit runs.
    ColliderFitResult result;std::string ignored;
    auto* collider=state.ColliderInterface();
    if(!CalculateColliderFit(*collider,result,ignored)||!result.HasBounds)return;
    state.Settings.Center=result.Center;
    if(auto* box=dynamic_cast<BoxColliderComponent*>(&state))box->Shape=result.Box;
    else if(auto* sphere=dynamic_cast<SphereColliderComponent*>(&state))sphere->Shape=result.Sphere;
    else if(auto* capsule=dynamic_cast<CapsuleColliderComponent*>(&state))capsule->Shape=result.Capsule;
}
}
