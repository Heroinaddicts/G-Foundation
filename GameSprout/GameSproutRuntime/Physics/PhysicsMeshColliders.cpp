#include "RuntimePhysicsSystem.h"
#include "MeshColliderGeometry.h"
#include "PhysicsConversions.h"
#include "Resources/BuiltinMeshes.h"
#include "Api/RuntimeBuiltinResources.h"

namespace GameSproutRuntime {
bool RuntimePhysicsSystem::AppendMeshCollider(BodyRequest &request,MeshColliderComponent &component,
    GPhysics::Shape &shape,const Mat4 &relative,std::string &error) {
    const auto settings=component.Shape;
    if(settings.Mesh.Guid.empty())return true;
    const auto sourceKey=std::make_pair(settings.Mesh.Guid,settings.Mesh.LocalID);
    auto source=_MeshSources.find(sourceKey);
    if(source==_MeshSources.end()) {
        MeshRenderData data;
        if(IsBuiltinResource(settings.Mesh)) {
            if(!BuiltinMeshes::Load(settings.Mesh,data,error))return false;
        } else {
            if(!_Provider){error="MeshCollider requires a prepared mesh resource provider";return false;}
            if(!_Provider->LoadMesh(settings.Mesh,data,error))return false;
        }
        GPhysics::MeshGeometry decoded;
        if(!DecodeMeshColliderGeometry(data,decoded,error))return false;
        source=_MeshSources.emplace(sourceKey,std::move(decoded)).first;
    }
    if(source->second.Vertices.empty())return true; // Missing identity retains its reference.
    if(!settings.Convex && (request.Description.Motion==GPhysics::MotionType::Dynamic||shape.Trigger)) {
        error="Non-convex MeshCollider cannot use a Dynamic Rigidbody or be a Trigger; enable Convex or use static/kinematic geometry";return false;
    }

    auto &cache=_MeshShapes[shape.Collider];
    if(!cache.Revision || cache.Reference!=settings.Mesh || !PhysicsNear(cache.Relative,relative)) {
        GPhysics::MeshGeometry transformed;
        if(!TransformMeshColliderGeometry(source->second,relative,transformed,error))return false;
        cache.Reference=settings.Mesh;cache.Relative=relative;cache.Geometry=std::move(transformed);
        cache.Revision=++_NextMeshRevision;
    }
    shape.Type=settings.Convex?GPhysics::ShapeType::ConvexHull:GPhysics::ShapeType::TriangleMesh;
    shape.LocalPose={};shape.Mesh=&cache.Geometry;shape.GeometryRevision=cache.Revision;
    return true;
}
}
