#include "GPhysics.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void Check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
GPhysics::MeshGeometry Tetrahedron() {
    return {{{-1,0,-1},{1,0,-1},{0,0,1},{0,2,0}},{0,2,1,0,1,3,1,2,3,2,0,3}};
}
}
int main() {
    try {
        std::string error;GPhysics::MeshGeometry hull;
        auto input=Tetrahedron();
        Check(GPhysics::CookMeshGeometry(input,true,hull,error)&&hull.Vertices.size()==4&&hull.Indices.size()==12,error);
        auto invalid=input;invalid.Indices[0]=500;
        Check(!GPhysics::CookMeshGeometry(invalid,false,hull,error)&&!error.empty(),"Out-of-range mesh index cooked");
        invalid=input;invalid.Vertices[0].X=std::numeric_limits<float>::quiet_NaN();
        Check(!GPhysics::CookMeshGeometry(invalid,true,hull,error),"NaN mesh position cooked");
        invalid=input;invalid.Indices[0]=invalid.Indices[1];
        Check(!GPhysics::CookMeshGeometry(invalid,false,hull,error),"Degenerate triangle cooked");
        GPhysics::World world;Check(world.Initialize(error),error);
        GPhysics::MeshGeometry plane{{{-10,0,-10},{10,0,-10},{10,0,10},{-10,0,10}},{0,2,1,0,3,2}};
        GPhysics::BodySettings floor;floor.Key=1;GPhysics::Shape shape;shape.Collider=11;shape.Type=GPhysics::ShapeType::TriangleMesh;
        shape.Mesh=&plane;shape.GeometryRevision=1;floor.Shapes={shape};
        Check(world.PutBody(floor,error),error);
        GPhysics::RayHit hit;Check(world.Raycast({0,4,0},{0,-10,0},~0u,false,hit)&&hit.Collider==11&&std::fabs(hit.Position.Y)<.001f,"Triangle mesh query lost leaf identity");
        auto kinematic=floor;kinematic.Key=3;kinematic.Motion=GPhysics::MotionType::Kinematic;kinematic.Transform.Position={30,0,0};kinematic.Shapes[0].Collider=33;
        Check(world.PutBody(kinematic,error),error);
        auto bad=kinematic;bad.Motion=GPhysics::MotionType::Dynamic;
        Check(!world.PutBody(bad,error)&&world.ContainsBody(3),"Dynamic nonconvex replacement did not fail atomically");
        bad=kinematic;bad.Shapes[0].Trigger=true;
        Check(!world.PutBody(bad,error),"Nonconvex trigger was accepted");
        GPhysics::BodySettings actor;actor.Key=2;actor.AllowedDOFs=7;actor.Motion=GPhysics::MotionType::Dynamic;actor.Transform.Position={0,5,0};
        shape.Type=GPhysics::ShapeType::ConvexHull;shape.Mesh=&input;shape.Collider=22;shape.GeometryRevision=2;actor.Shapes={shape};
        Check(world.PutBody(actor,error),error);
        unsigned enter=0;std::vector<GPhysics::Contact> contacts;
        for(unsigned i=0;i<150;++i){Check(world.Step(1.f/60,contacts,error),error);for(const auto &event:contacts)if(event.State==GPhysics::Contact::Phase::Enter&&event.FirstCollider==11&&event.SecondCollider==22)++enter;}
        GPhysics::BodyState state;Check(world.GetBodyState(2,state)&&state.Transform.Position.Y>-.1f&&state.Transform.Position.Y<.1f&&enter==1,"Convex body failed to settle/callback on triangle surface");
        Check(world.Raycast({0,4,0},{0,-10,0},~0u,false,hit,{22})&&hit.Collider==11,"Mesh leaf exclusion removed unrelated triangle floor");
        std::cout<<"PASS convex cooking, triangle static/kinematic, collision, leaf filtering and invalid meshes\n";return 0;
    }catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
