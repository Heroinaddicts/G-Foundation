#include "MeshShapeCache.h"
#include <bit>
#include <cstring>

namespace GPhysics::Detail {
namespace {
uint64_t Fingerprint(const MeshGeometry& geometry,bool convex) {
    uint64_t hash=14695981039346656037ull;
    const auto word=[&](uint64_t value) {hash^=value;hash*=1099511628211ull;hash^=hash>>32;};
    word(convex);word(geometry.Vertices.size());word(geometry.Indices.size());
    for(const auto vertex:geometry.Vertices) {
        word(std::bit_cast<uint32_t>(vertex.X));word(std::bit_cast<uint32_t>(vertex.Y));word(std::bit_cast<uint32_t>(vertex.Z));
    }
    for(const auto index:geometry.Indices)word(index);
    return hash;
}
bool SameGeometry(const MeshGeometry& a,const MeshGeometry& b) {
    if(a.Vertices.size()!=b.Vertices.size()||a.Indices.size()!=b.Indices.size())return false;
    if(!a.Indices.empty()&&std::memcmp(a.Indices.data(),b.Indices.data(),a.Indices.size()*sizeof(uint32_t)))return false;
    // Packed float triples can be compared in one byte span. On a target with
    // padding, compare component bits instead. Never round/quantize positions;
    // even signed zero remains a distinct content identity.
    if constexpr(sizeof(Vector3)==3*sizeof(float)) {
        return a.Vertices.empty()||!std::memcmp(a.Vertices.data(),b.Vertices.data(),a.Vertices.size()*sizeof(Vector3));
    }
    for(std::size_t index=0;index<a.Vertices.size();++index) {
        const auto x=a.Vertices[index],y=b.Vertices[index];
        if(std::bit_cast<uint32_t>(x.X)!=std::bit_cast<uint32_t>(y.X)||
           std::bit_cast<uint32_t>(x.Y)!=std::bit_cast<uint32_t>(y.Y)||
           std::bit_cast<uint32_t>(x.Z)!=std::bit_cast<uint32_t>(y.Z))return false;
    }
    return true;
}
}
JPH::Shape::ShapeResult MeshShapeCache::Cook(const MeshGeometry& geometry,bool convex,uint64_t collider) {
    // Compound leaves must preserve their individual userdata. They never enter
    // the cache, including when the same geometry is already retained here.
    if(collider) {++_Statistics.CookCalls;return CreateMeshShape(geometry,convex,collider);}
    const auto hash=_Fingerprint?_Fingerprint(geometry,convex):Fingerprint(geometry,convex);
    const auto range=_Entries.equal_range(hash);
    for(auto item=range.first;item!=range.second;++item)
        if(item->second.Convex==convex&&SameGeometry(item->second.Geometry,geometry)) {
            ++_Statistics.CacheHits;JPH::Shape::ShapeResult result;result.Set(item->second.Cooked);return result;
        }
    ++_Statistics.CookCalls;
    auto result=CreateMeshShape(geometry,convex,0);
    if(result.HasError())return result;
    // Include both the owned comparison input and Jolt's reported leaf memory.
    // Bodies retain their own Jolt references; this limits extra cache retention,
    // not the memory required by live physics bodies.
    const uint64_t bytes=uint64_t(geometry.Vertices.size())*sizeof(Vector3)+
        uint64_t(geometry.Indices.size())*sizeof(uint32_t)+result.Get()->GetStats().mSizeBytes+sizeof(Entry)+4*sizeof(void*);
    if(bytes<=_Budget&&_Statistics.RetainedBytes<=_Budget-bytes) {
        Entry entry;entry.Geometry=geometry;entry.Convex=convex;entry.Cooked=result.Get();
        _Entries.emplace(hash,std::move(entry));
        _Statistics.RetainedBytes+=bytes;++_Statistics.CachedShapes;
    }
    return result;
}
}
