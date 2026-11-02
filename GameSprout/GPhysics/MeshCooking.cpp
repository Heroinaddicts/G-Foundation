#include "MeshCooking.h"
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <cmath>
#include <limits>

namespace GPhysics::Detail {
JPH::Shape::ShapeResult CreateMeshShape(const MeshGeometry &geometry, bool convex, uint64_t collider) {
    const auto fail=[](const char *message) {
        JPH::Shape::ShapeResult result;result.SetError(message);return result;
    };
    if (geometry.Vertices.empty() || geometry.Indices.empty() || geometry.Indices.size()%3 != 0 ||
        geometry.Vertices.size()>std::numeric_limits<uint32_t>::max())
        return fail("MeshCollider needs indexed triangle geometry");
    for (const auto point : geometry.Vertices)
        if (!std::isfinite(point.X)||!std::isfinite(point.Y)||!std::isfinite(point.Z))
            return fail("MeshCollider contains non-finite positions");
    for (size_t i=0;i<geometry.Indices.size();i+=3) {
        const auto a=geometry.Indices[i],b=geometry.Indices[i+1],c=geometry.Indices[i+2];
        if (a>=geometry.Vertices.size()||b>=geometry.Vertices.size()||c>=geometry.Vertices.size())
            return fail("MeshCollider index is outside the vertex buffer");
        const auto first=geometry.Vertices[a],second=geometry.Vertices[b],third=geometry.Vertices[c];
        const double x1=double(second.X)-first.X,y1=double(second.Y)-first.Y,z1=double(second.Z)-first.Z;
        const double x2=double(third.X)-first.X,y2=double(third.Y)-first.Y,z2=double(third.Z)-first.Z;
        const double x=y1*z2-z1*y2,y=z1*x2-x1*z2,z=x1*y2-y1*x2;
        if (x*x+y*y+z*z==0) return fail("MeshCollider contains a degenerate triangle");
    }
    if (convex) {
        JPH::ConvexHullShapeSettings source;
        source.mMaxConvexRadius=0;
        source.mUserData=collider;
        source.mPoints.reserve(geometry.Vertices.size());
        // Only referenced positions belong to the collision geometry; repeated
        // triangle indices must not multiply the hull builder input.
        std::vector<bool> included(geometry.Vertices.size());
        for (auto index:geometry.Indices) if(!included[index]) {
            included[index]=true;
            const auto point=geometry.Vertices[index];source.mPoints.emplace_back(point.X,point.Y,point.Z);
        }
        return source.Create();
    }
    JPH::VertexList points;points.reserve(geometry.Vertices.size());
    for (auto point:geometry.Vertices) points.emplace_back(point.X,point.Y,point.Z);
    JPH::IndexedTriangleList triangles;triangles.reserve(geometry.Indices.size()/3);
    for(size_t i=0;i<geometry.Indices.size();i+=3)
        triangles.emplace_back(geometry.Indices[i],geometry.Indices[i+1],geometry.Indices[i+2],0);
    JPH::MeshShapeSettings source(std::move(points),std::move(triangles));
    source.mUserData=collider;
    return source.Create();
}
}
namespace GPhysics {
bool CookMeshGeometry(const MeshGeometry &input,bool convex,MeshGeometry &output,std::string &error) {
    output={};error.clear();
    Detail::AcquireMeshBackend();
    struct Guard { ~Guard(){Detail::ReleaseMeshBackend();} } guard;
    const auto result=Detail::CreateMeshShape(input,convex,0);
    if(result.HasError()){error=result.GetError().c_str();return false;}
    if(!convex){output=input;return true;}
    const auto *hull=static_cast<const JPH::ConvexHullShape*>(result.Get().GetPtr());
    const auto center=hull->GetCenterOfMass();
    output.Vertices.reserve(hull->GetNumPoints());
    for(JPH::uint i=0;i<hull->GetNumPoints();++i) {
        const auto point=hull->GetPoint(i)+center;
        output.Vertices.push_back({point.GetX(),point.GetY(),point.GetZ()});
    }
    JPH::uint indices[JPH::ConvexHullShape::cMaxPointsInHull];
    for(JPH::uint face=0;face<hull->GetNumFaces();++face) {
        const auto count=hull->GetFaceVertices(face,JPH::ConvexHullShape::cMaxPointsInHull,indices);
        for(JPH::uint i=1;i+1<count;++i) {
            output.Indices.push_back(indices[0]);output.Indices.push_back(indices[i]);output.Indices.push_back(indices[i+1]);
        }
    }
    return true;
}
}
