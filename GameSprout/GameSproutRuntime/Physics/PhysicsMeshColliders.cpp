#include "RuntimePhysicsSystem.h"
#include "MeshColliderGeometry.h"
#include "PhysicsConversions.h"
#include "Resources/BuiltinMeshes.h"
#include "Api/RuntimeBuiltinResources.h"
#include "Api/RuntimeColliderMath.h"

namespace GameSproutRuntime {
bool RuntimePhysicsSystem::AppendMeshCollider(BodyRequest &request,MeshColliderComponent &component,
    GPhysics::Shape &shape,const Mat4 &relative,std::string &error) {
    const auto& settings=component.Shape;
    if(settings.Mesh.Guid.empty())return true;
    const auto sourceKey=std::make_pair(settings.Mesh.Guid,settings.Mesh.LocalID);
    const bool builtin=IsBuiltinResource(settings.Mesh);
    RuntimePhysicsMath::ColliderFrame frame;
    if(!builtin && _Provider && RuntimePhysicsMath::TryGetColliderFrame(relative,frame)) {
        auto& prepared=_PreparedMeshSources[sourceKey][settings.Convex?1:0];
        if(!prepared.Attempted) {
            prepared.Attempted=true;
            std::vector<std::uint8_t> bytes;
            std::string loadError;
            if(_Provider->LoadMeshCollision(settings.Mesh,settings.Convex,bytes,loadError) && !bytes.empty() &&
                prepared.Mesh.Restore(bytes,settings.Convex,loadError))
                prepared.Revision=++_NextMeshRevision;
            _ResourceRevisions[sourceKey]=_Provider->GetResourceRevision(settings.Mesh);
        }
        const auto scale=PhysicsVector(frame.Scale);
        if(prepared.Revision && prepared.Mesh.SupportsScale(scale)) {
            shape.LocalPose={PhysicsVector(frame.Position),PhysicsRotation(frame.Rotation)};
            shape.MeshScale=scale;
            shape.PreparedMesh=&prepared.Mesh;
            shape.GeometryRevision=prepared.Revision;
        }
    }
    // The cooked shape stays in asset space. Sheared transforms and triangle
    // scales that change winding/active edges still need exact affine cooking.
    if(!shape.PreparedMesh) {
        auto source=_MeshSources.find(sourceKey);
        if(source==_MeshSources.end()) {
            MeshRenderData data;
            if(builtin) {
                if(!BuiltinMeshes::Load(settings.Mesh,data,error))return false;
            } else {
                if(!_Provider){error="MeshCollider requires a prepared mesh resource provider";return false;}
                if(!_Provider->LoadMesh(settings.Mesh,data,error))return false;
            }
            GPhysics::MeshGeometry decoded;
            if(!DecodeMeshColliderGeometry(data,decoded,error))return false;
            source=_MeshSources.emplace(sourceKey,std::move(decoded)).first;
            if(!builtin)
                _ResourceRevisions[sourceKey]=_Provider->GetResourceRevision(settings.Mesh);
        }
        if(source->second.Vertices.empty())return true; // Missing identity retains its reference.
        auto &cache=_MeshShapes[shape.Collider];
        if(!cache.Revision || cache.Reference!=settings.Mesh || !PhysicsNear(cache.Relative,relative)) {
            GPhysics::MeshGeometry transformed;
            if(!TransformMeshColliderGeometry(source->second,relative,transformed,error))return false;
            cache.Reference=settings.Mesh;cache.Relative=relative;cache.Geometry=std::move(transformed);
            cache.Revision=++_NextMeshRevision;
        }
        shape.LocalPose={};shape.Mesh=&cache.Geometry;shape.GeometryRevision=cache.Revision;
    }
    if(!settings.Convex && (request.Description.Motion==GPhysics::MotionType::Dynamic||shape.Trigger)) {
        error="Non-convex MeshCollider cannot use a Dynamic Rigidbody or be a Trigger; enable Convex or use static/kinematic geometry";return false;
    }
    shape.Type=settings.Convex?GPhysics::ShapeType::ConvexHull:GPhysics::ShapeType::TriangleMesh;
    return true;
}
}
