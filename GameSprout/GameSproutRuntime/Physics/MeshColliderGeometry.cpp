#include "MeshColliderGeometry.h"
#include "Api/Math/TransformMath.h"
#include <cstring>
#include <limits>

namespace GameSproutRuntime {
bool DecodeMeshColliderGeometry(const MeshRenderData &source,GPhysics::MeshGeometry &output,std::string &error) {
    return DecodeMeshColliderGeometry(MeshCollisionBakeInput{source.Bytes,source.VertexCount,source.VertexStride,
        source.IndexCount,source.PositionOffset,source.IndexOffset},output,error);
}
bool DecodeMeshColliderGeometry(const MeshCollisionBakeInput &source,GPhysics::MeshGeometry &output,std::string &error) {
    output={};error.clear();
    const uint64_t vertexBytes=uint64_t(source.VertexCount)*source.VertexStride;
    const uint64_t indexBytes=uint64_t(source.IndexCount)*sizeof(uint32_t);
    if(!source.VertexCount&&!source.IndexCount&&source.Bytes.empty())return true; // Missing source.
    if(!source.VertexCount||!source.IndexCount||source.IndexCount%3||source.VertexStride<12||
       source.PositionOffset>source.VertexStride-12||vertexBytes>source.Bytes.size()||
       source.IndexOffset<vertexBytes||source.IndexOffset>source.Bytes.size()||
       indexBytes>source.Bytes.size()-source.IndexOffset) {
        error="Invalid prepared MeshCollider vertex/index layout";return false;
    }
    GPhysics::MeshGeometry geometry;
    geometry.Vertices.reserve(source.VertexCount);
    for(uint32_t i=0;i<source.VertexCount;++i) {
        GPhysics::Vector3 point;
        std::memcpy(&point,source.Bytes.data()+uint64_t(i)*source.VertexStride+source.PositionOffset,12);
        if(!std::isfinite(point.X)||!std::isfinite(point.Y)||!std::isfinite(point.Z)) {
            error="MeshCollider positions must be finite";return false;
        }
        geometry.Vertices.push_back(point);
    }
    geometry.Indices.resize(source.IndexCount);
    std::memcpy(geometry.Indices.data(),source.Bytes.data()+source.IndexOffset,indexBytes);
    for(auto index:geometry.Indices)if(index>=geometry.Vertices.size()) {
        error="MeshCollider index exceeds the prepared vertex count";return false;
    }
    output=std::move(geometry);return true;
}
bool TransformMeshColliderGeometry(const GPhysics::MeshGeometry &source,const Mat4 &matrix,
                                  GPhysics::MeshGeometry &output,std::string &error) {
    output={};error.clear();Mat4 inverse;
    if(!RuntimeMath::TryInverseAffine(matrix,inverse)) {
        error="MeshCollider transform must be a finite invertible affine transform";return false;
    }
    GPhysics::MeshGeometry geometry;geometry.Indices=source.Indices;geometry.Vertices.reserve(source.Vertices.size());
    for(auto point:source.Vertices) {
        const auto transformed=matrix*Vector4(point.X,point.Y,point.Z,1);
        if(!std::isfinite(transformed.X)||!std::isfinite(transformed.Y)||!std::isfinite(transformed.Z)) {
            error="Transformed MeshCollider position is not finite";return false;
        }
        geometry.Vertices.push_back({transformed.X,transformed.Y,transformed.Z});
    }
    const double determinant=double(matrix(0,0))*(double(matrix(1,1))*matrix(2,2)-double(matrix(1,2))*matrix(2,1))
        -double(matrix(0,1))*(double(matrix(1,0))*matrix(2,2)-double(matrix(1,2))*matrix(2,0))
        +double(matrix(0,2))*(double(matrix(1,0))*matrix(2,1)-double(matrix(1,1))*matrix(2,0));
    if(determinant<0)for(size_t i=0;i+2<geometry.Indices.size();i+=3)std::swap(geometry.Indices[i+1],geometry.Indices[i+2]);
    output=std::move(geometry);return true;
}
}
