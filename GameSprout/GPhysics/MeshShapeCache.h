#pragma once
#include "MeshCooking.h"
#include <cstddef>
#include <map>

namespace GPhysics::Detail {
// World-owned immutable Jolt leaves. Identity/material/filtering remain on each
// BodyRecord; only single-mesh bodies may use a zero-userdata cached leaf.
class MeshShapeCache final {
public:
    using FingerprintFunction=uint64_t(*)(const MeshGeometry&,bool);
    explicit MeshShapeCache(std::size_t budget=32u*1024u*1024u,FingerprintFunction fingerprint=nullptr)
        :_Budget(budget),_Fingerprint(fingerprint) {}
    JPH::Shape::ShapeResult Cook(const MeshGeometry&,bool convex,uint64_t collider);
    MeshCookStatistics GetStatistics() const {return _Statistics;}
private:
    struct Entry {
        MeshGeometry Geometry;
        bool Convex=false;
        JPH::Ref<JPH::Shape> Cooked;
    };
    std::multimap<uint64_t,Entry> _Entries;
    std::size_t _Budget;
    FingerprintFunction _Fingerprint;
    MeshCookStatistics _Statistics;
};
}
