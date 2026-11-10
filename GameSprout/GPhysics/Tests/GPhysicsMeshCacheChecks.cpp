#include "GPhysics.h"
#include "MeshShapeCache.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
GPhysics::MeshGeometry Tetrahedron() {
    return {{{-1,0,-1},{1,0,-1},{0,0,1},{0,2,0}},{0,2,1,0,1,3,1,2,3,2,0,3}};
}
GPhysics::MeshGeometry Plane() {
    return {{{-10,0,-10},{10,0,-10},{10,0,10},{-10,0,10}},{0,2,1,0,3,2}};
}
uint64_t CollidingFingerprint(const GPhysics::MeshGeometry&,bool){return 0;}
void CollisionSafetyAndBudget() {
    std::string error;GPhysics::World lifetime;Check(lifetime.Initialize(error),error);
    GPhysics::Detail::MeshShapeCache cache(1024*1024,CollidingFingerprint);
    const auto mesh=Tetrahedron();auto changed=mesh;changed.Vertices[3].Y=3;
    Check(!cache.Cook(mesh,false,0).HasError()&&!cache.Cook(changed,false,0).HasError(),"Valid mesh failed cooking");
    Check(cache.GetStatistics().CookCalls==2&&cache.GetStatistics().CacheHits==0,
        "Hash collision aliased different vertex contents");
    Check(!cache.Cook(mesh,false,0).HasError()&&cache.GetStatistics().CacheHits==1,
        "Exact content failed collision-safe cache lookup");
    auto changedIndices=mesh;std::swap(changedIndices.Indices[0],changedIndices.Indices[1]);
    Check(!cache.Cook(changedIndices,false,0).HasError()&&cache.GetStatistics().CookCalls==3,
        "Changed triangle winding reused another index buffer");
    Check(!cache.Cook(mesh,true,0).HasError()&&cache.GetStatistics().CookCalls==4,
        "Convex and triangle recipes shared the same cache entry");
    auto signedZero=mesh;signedZero.Vertices[0].Y=-0.0f;
    Check(!cache.Cook(signedZero,false,0).HasError()&&cache.GetStatistics().CookCalls==5,
        "Cache rounded/normalized float bits");
    auto invalid=mesh;invalid.Indices[0]=999;
    Check(cache.Cook(invalid,false,0).HasError()&&cache.GetStatistics().CachedShapes==5,
        "Invalid geometry entered the cache");
    Check(!cache.Cook(mesh,false,123).HasError()&&cache.GetStatistics().CookCalls==7,
        "Identity-bearing compound leaf entered a shared zero-userdata cache");
    GPhysics::Detail::MeshShapeCache tiny(1);
    Check(!tiny.Cook(mesh,false,0).HasError()&&!tiny.Cook(mesh,false,0).HasError(),"Budget fallback failed ordinary cooking");
    const auto statistics=tiny.GetStatistics();
    Check(statistics.CookCalls==2&&!statistics.CacheHits&&!statistics.CachedShapes&&!statistics.RetainedBytes,
        "Budget overflow retained shapes or silently reused an untracked shape");
}
void BodiesAndQueries() {
    std::string error;GPhysics::World world;Check(world.Initialize(error),error);
    auto mesh=Plane();
    GPhysics::BodySettings first;first.Key=1;GPhysics::Shape shape;
    shape.Collider=0x100000001ull;shape.Type=GPhysics::ShapeType::TriangleMesh;
    shape.Mesh=&mesh;shape.GeometryRevision=1;shape.Layer=3;first.Shapes={shape};
    Check(world.PutBody(first,error),error);
    auto second=first;second.Key=2;second.Transform.Position.Y=-3;
    second.Shapes[0].Collider=0x200000001ull;second.Shapes[0].Surface.Friction=0;
    Check(world.PutBody(second,error),error);
    auto statistics=world.GetMeshCookStatistics();
    Check(statistics.CookCalls==1&&statistics.CacheHits==1&&statistics.CachedShapes==1&&statistics.RetainedBytes>0,
        "Identical single-mesh bodies were cooked separately");
    GPhysics::RayHit hit;
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==first.Shapes[0].Collider,
        "Shared mesh lost the first body's full 64-bit collider identity");
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit,{first.Shapes[0].Collider})&&hit.Collider==second.Shapes[0].Collider,
        "Ignoring first body hid a distinct body sharing the same mesh");
    first.Shapes[0].Layer=4;Check(world.PutBody(first,error),error);
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==second.Shapes[0].Collider,
        "Shared leaf reused another body's changed layer");
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<4,false,hit)&&hit.Collider==first.Shapes[0].Collider,
        "Body replacement did not update the raycast layer independently");
    world.RemoveBody(1);
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==second.Shapes[0].Collider,
        "Removing one body destroyed another body's shared shape");

    auto triggerGeometry=Tetrahedron();GPhysics::BodySettings trigger;trigger.Key=3;
    shape.Mesh=&triggerGeometry;shape.Type=GPhysics::ShapeType::ConvexHull;shape.Collider=0x300000001ull;
    shape.Trigger=true;shape.Layer=3;trigger.Transform.Position.Y=1;trigger.Shapes={shape};
    Check(world.PutBody(trigger,error),error);
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,true,hit)&&hit.Collider==shape.Collider,
        "Shared convex trigger lost identity");
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==second.Shapes[0].Collider,
        "Trigger exclusion stopped at an ignored shared leaf");
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,true,hit,{shape.Collider})&&hit.Collider==second.Shapes[0].Collider,
        "Ignoring convex trigger hid an unrelated mesh body");
    trigger.Shapes[0].Trigger=false;Check(world.PutBody(trigger,error),error);
    Check(world.Raycast({0,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==shape.Collider,
        "Trigger changes remained pinned to cached shape metadata");

    // A compound identifies shared mesh leaves through owner-local settings.
    GPhysics::BodySettings compound;compound.Key=4;compound.Transform.Position.X=30;
    shape.Mesh=&mesh;shape.Type=GPhysics::ShapeType::TriangleMesh;shape.Trigger=false;
    shape.Collider=0x400000001ull;shape.LocalPose.Position.Y=2;compound.Shapes={shape};
    shape.Collider=0x400000002ull;shape.LocalPose.Position.Y=0;compound.Shapes.push_back(shape);
    const auto beforeCompound=world.GetMeshCookStatistics();Check(world.PutBody(compound,error),error);
    const auto afterCompound = world.GetMeshCookStatistics();
    Check(afterCompound.CookCalls == beforeCompound.CookCalls && afterCompound.CacheHits == beforeCompound.CacheHits + 2,
        "Compound leaves did not reuse identical mesh geometry");
    Check(world.Raycast({30,4,0},{0,-12,0},1u<<3,false,hit)&&hit.Collider==compound.Shapes[0].Collider,
        "Compound mesh leaf identity was lost");
    Check(world.Raycast({30,4,0},{0,-12,0},1u<<3,false,hit,{compound.Shapes[0].Collider})&&hit.Collider==compound.Shapes[1].Collider,
        "Compound leaf exclusion removed both mesh leaves");

    // Repeated instances vary transforms and identities, but not local geometry.
    const auto beforeMany=world.GetMeshCookStatistics();
    for(unsigned index=0;index<400;++index) {
        auto body=second;body.Key=100+index;body.Transform.Position={100+25.0f*index,0,0};
        body.Shapes[0].Collider=0x1000000000ull+index;Check(world.PutBody(body,error),error);
    }
    statistics=world.GetMeshCookStatistics();
    Check(statistics.CookCalls==beforeMany.CookCalls&&statistics.CacheHits==beforeMany.CacheHits+400&&
        statistics.RetainedBytes<=32u*1024u*1024u,"Repeated bodies rebuilt BVHs or exceeded retained cache budget");
    auto changed=mesh;changed.Vertices[0].Y=.1f;
    auto modified=second;modified.Key=900;modified.Transform.Position.X=10000;modified.Shapes[0].Collider=900;
    modified.Shapes[0].Mesh=&changed;modified.Shapes[0].GeometryRevision=2;
    Check(world.PutBody(modified,error),error);
    Check(world.GetMeshCookStatistics().CookCalls==statistics.CookCalls+1,
        "Changed local mesh positions reused an old BVH");
    statistics=world.GetMeshCookStatistics();
    std::cout<<"Mesh cache fixture: cookCalls="<<statistics.CookCalls<<" hits="<<statistics.CacheHits
        <<" retainedShapes="<<statistics.CachedShapes<<" retainedBytes="<<statistics.RetainedBytes
        <<" budgetBytes="<<32u*1024u*1024u<<'\n';
}
void CacheSurvivesWorldReplacement() {
    GPhysics::MeshCookCache cache;
    std::string error;
    const auto mesh = Plane();
    GPhysics::BodySettings body;
    body.Key = 1;
    GPhysics::Shape shape;
    shape.Collider = 101;
    shape.Type = GPhysics::ShapeType::TriangleMesh;
    shape.Mesh = &mesh;
    shape.GeometryRevision = 1;
    body.Shapes = {shape};
    {
        GPhysics::World world;
        Check(world.Initialize(error, &cache) && world.PutBody(body, error), error);
        Check(world.GetMeshCookStatistics().CookCalls == 1, "First world did not cook its mesh");
    }

    GPhysics::World world;
    body.Shapes[0].Collider = 202;
    body.Transform.Position.Y = -2;
    Check(world.Initialize(error, &cache) && world.PutBody(body, error), error);
    const auto stats = world.GetMeshCookStatistics();
    Check(stats.CookCalls == 1 && stats.CacheHits == 1,
        "Identical geometry was recooked after the first world was destroyed");
    GPhysics::RayHit hit;
    Check(world.Raycast({0, 4, 0}, {0, -10, 0}, 0xffffffffu, false, hit) &&
        hit.Collider == 202 && std::fabs(hit.Position.Y + 2) < .001f,
        "A new world retained the previous body's identity or position");
}
void CookedArtifacts() {
    std::string error;
    std::vector<std::uint8_t> triangleBytes, convexBytes;
    Check(GPhysics::MeshArtifactFormat() != 0, "Artifact format has no identity");
    Check(GPhysics::CookMeshArtifact(Plane(), false, triangleBytes, error), error);
    Check(GPhysics::CookMeshArtifact(Tetrahedron(), true, convexBytes, error), error);
    Check(!GPhysics::CookMeshArtifact({}, true, convexBytes, error), "Invalid geometry produced an artifact");
    GPhysics::CookedMesh triangle, convex;
    Check(triangle.Restore(triangleBytes, false, error), error);
    Check(convex.Restore(convexBytes, true, error), error);
    Check(triangle.SupportsScale({2, 2, 2}) && !triangle.SupportsScale({-2, 2, 2}) &&
        !triangle.SupportsScale({2, 2, 3}) && convex.SupportsScale({-2, 2, 3}) &&
        !convex.SupportsScale({2, 0, 2}), "Prepared scale support does not match raw mesh fallback");
    Check(!triangle.Restore(convexBytes, false, error), "Convex artifact loaded as a triangle mesh");
    Check(!triangle.Restore(std::span(triangleBytes).first(triangleBytes.size() - 1), false, error),
        "Truncated artifact restored");
    auto corrupted = triangleBytes;
    corrupted.back() ^= 1;
    Check(!triangle.Restore(corrupted, false, error), "Corrupted payload restored");
    corrupted = triangleBytes;
    corrupted[8] ^= 1;
    Check(!triangle.Restore(corrupted, false, error), "Incompatible format restored");

    GPhysics::World world;
    Check(world.Initialize(error), error);
    GPhysics::Shape shape;
    shape.Collider = 0x100000001ull;
    shape.Type = GPhysics::ShapeType::TriangleMesh;
    shape.PreparedMesh = &triangle;
    shape.GeometryRevision = 1;
    shape.MeshScale = {2, 2, 2};
    shape.LocalPose.Position.Y = 1;
    shape.Layer = 3;
    GPhysics::BodySettings first;
    first.Key = 1;
    first.Shapes = {shape};
    Check(world.PutBody(first, error), error);
    auto second = first;
    second.Key = 2;
    second.Transform.Position.Y = -2;
    second.Shapes[0].Collider = 0x200000001ull;
    Check(world.PutBody(second, error), error);
    GPhysics::BodySettings compound;
    compound.Key = 3;
    compound.Transform.Position.X = 80;
    shape.Collider = 0x300000001ull;
    compound.Shapes.push_back(shape);
    shape.Collider = 0x300000002ull;
    shape.LocalPose.Position.Y = 2;
    compound.Shapes.push_back(shape);
    Check(world.PutBody(compound, error), error);
    triangle = {};
    GPhysics::RayHit hit;
    Check(world.Raycast({15, 4, 0}, {0, -12, 0}, 1u << 3, false, hit) &&
        hit.Collider == first.Shapes[0].Collider && std::fabs(hit.Position.Y - 1) < .001f,
        "Prepared shape lost scale, local pose, lifetime or collider identity");
    Check(world.Raycast({15, 4, 0}, {0, -12, 0}, 1u << 3, false, hit, {first.Shapes[0].Collider}) &&
        hit.Collider == second.Shapes[0].Collider && std::fabs(hit.Position.Y + 1) < .001f,
        "Sharing a prepared mesh merged body identities");
    Check(world.Raycast({95, 4, 0}, {0, -12, 0}, 1u << 3, false, hit) &&
        hit.Collider == compound.Shapes[1].Collider &&
        world.Raycast({95, 4, 0}, {0, -12, 0}, 1u << 3, false, hit, {compound.Shapes[1].Collider}) &&
        hit.Collider == compound.Shapes[0].Collider,
        "Prepared compound leaves lost their owner-local metadata");
    Check(world.GetMeshCookStatistics().CookCalls == 0, "Prepared bodies unexpectedly ran the mesh cooker");
}
void ContactsAndMaterials(bool usePrepared) {
    std::string error;GPhysics::World world;Check(world.Initialize(error),error);
    auto plane=Plane(),hull=Tetrahedron();GPhysics::Shape shape;
    GPhysics::CookedMesh cookedPlane, cookedHull;
    if (usePrepared) {
        std::vector<std::uint8_t> bytes;
        Check(GPhysics::CookMeshArtifact(plane, false, bytes, error) && cookedPlane.Restore(bytes, false, error), error);
        Check(GPhysics::CookMeshArtifact(hull, true, bytes, error) && cookedHull.Restore(bytes, true, error), error);
    }
    shape.Type=GPhysics::ShapeType::TriangleMesh;shape.Mesh=&plane;shape.GeometryRevision=1;shape.Collider=100;
    if (usePrepared) {
        shape.Mesh = nullptr;
        shape.PreparedMesh = &cookedPlane;
    }
    shape.Surface.Friction=1;GPhysics::BodySettings floor;floor.Key=1;floor.Shapes={shape};
    Check(world.PutBody(floor,error),error);
    GPhysics::BodySettings slippery;slippery.Key=2;slippery.Motion=GPhysics::MotionType::Dynamic;
    slippery.AllowedDOFs=7;slippery.Transform.Position={-4,.01f,-3};slippery.LinearVelocity={4,0,0};slippery.LinearDamping=0;
    shape.Type=GPhysics::ShapeType::ConvexHull;shape.Mesh=&hull;shape.Collider=200;
    if (usePrepared) {
        shape.Mesh = nullptr;
        shape.PreparedMesh = &cookedHull;
        shape.MeshScale = {-1, 1, 1};
    }
    shape.Surface.Friction=0;shape.Surface.FrictionCombine=GPhysics::CombineMode::Minimum;slippery.Shapes={shape};
    auto sticky=slippery;sticky.Key=3;sticky.Transform.Position.Z=3;sticky.Shapes[0].Collider=300;
    sticky.Shapes[0].Surface.Friction=1;sticky.Shapes[0].Surface.FrictionCombine=GPhysics::CombineMode::Maximum;
    Check(world.PutBody(slippery,error)&&world.PutBody(sticky,error),error);
    const auto stats = world.GetMeshCookStatistics();
    Check(stats.CookCalls == (usePrepared ? 0 : 2) && stats.CacheHits == (usePrepared ? 0 : 1),
        "Dynamic convex bodies did not share their mesh geometry");
    std::vector<GPhysics::Contact> contacts;bool sawSlippery=false,sawSticky=false;
    for(unsigned index=0;index<120;++index) {
        Check(world.Step(1.f/60,contacts,error),error);
        for(const auto& event:contacts) {
            Check(event.FirstCollider!=0&&event.SecondCollider!=0,"Shared shape emitted zero collider identity");
            sawSlippery|=event.FirstCollider==100&&event.SecondCollider==200;
            sawSticky|=event.FirstCollider==100&&event.SecondCollider==300;
        }
    }
    GPhysics::BodyState free,stopped;
    Check(world.GetBodyState(2,free)&&world.GetBodyState(3,stopped)&&sawSlippery&&sawSticky,
        "Shared geometry lost independent contact callback identities");
    Check(free.LinearVelocity.X>3.5f&&std::fabs(stopped.LinearVelocity.X)<.5f,
        "Shared geometry merged per-body friction/combine materials");
}
}
int main() {
    try {CollisionSafetyAndBudget();BodiesAndQueries();CacheSurvivesWorldReplacement();CookedArtifacts();
        ContactsAndMaterials(false);ContactsAndMaterials(true);
        std::cout<<"PASS mesh cook cache: exact bits, bounded retention, independent world and collider state, compounds and repeated instances\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}
