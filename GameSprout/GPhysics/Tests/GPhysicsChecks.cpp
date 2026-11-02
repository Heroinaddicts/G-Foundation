#include "GPhysics.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void Materials() {
    GPhysics::Material first, second;
    first.Friction = 0.2f;
    second.Friction = 0.8f;
    Check(std::fabs(GPhysics::CombineMaterials(first, second).Friction - 0.5f) < 1e-6f,
          "Default friction combine is not Average");
    first.FrictionCombine = GPhysics::CombineMode::Minimum;
    second.FrictionCombine = GPhysics::CombineMode::Multiply;
    Check(std::fabs(GPhysics::CombineMaterials(first, second).Friction - 0.16f) < 1e-6f,
          "Multiply did not take priority over Minimum");
    Check(GPhysics::CombineMaterials(first, second) == GPhysics::CombineMaterials(second, first),
          "Material combine is not symmetric");
    second.FrictionCombine = GPhysics::CombineMode::Maximum;
    Check(GPhysics::CombineMaterials(first, second).Friction == 0.8f, "Maximum did not take priority");
    first.Friction = second.Friction = std::numeric_limits<float>::max();
    first.FrictionCombine = second.FrictionCombine = GPhysics::CombineMode::Multiply;
    const auto huge = GPhysics::CombineMaterials(first, second);
    Check(std::isfinite(huge.Friction) && huge.Friction == std::numeric_limits<float>::max(),
          "Finite materials overflowed their combined coefficient");
    first.FrictionCombine = second.FrictionCombine = GPhysics::CombineMode::Average;
    Check(std::isfinite(GPhysics::CombineMaterials(first, second).Friction),
          "Average combine overflowed while adding finite values");
}
void World() {
    std::string error;
    GPhysics::World world;
    Check(world.Initialize(error), error);
    GPhysics::BodySettings ground;
    ground.Key = 1;
    ground.Transform.Position = {0, -0.5f, 0};
    GPhysics::Shape floor;
    floor.Collider = 1;
    floor.HalfExtents = {20, 0.5f, 20};
    floor.Surface.Friction = 0.8f;
    floor.Surface.FrictionCombine = GPhysics::CombineMode::Multiply;
    ground.Shapes = {floor};
    Check(world.PutBody(ground, error), error);
    GPhysics::BodySettings slippery;
    slippery.Key = 2;
    slippery.Motion = GPhysics::MotionType::Dynamic;
    slippery.Transform.Position = {-4, 0.5f, -3};
    slippery.LinearVelocity = {4, 0, 0};
    slippery.LinearDamping = 0;
    GPhysics::Shape sliding;
    sliding.Collider = 2;
    sliding.Surface.Friction = 0;
    sliding.Surface.FrictionCombine = GPhysics::CombineMode::Minimum;
    slippery.Shapes = {sliding};
    Check(world.PutBody(slippery, error), error);
    GPhysics::BodySettings sticky = slippery;
    sticky.Key = 3;
    sticky.Transform.Position = {-4, 0.5f, 3};
    sticky.Shapes[0].Collider = 3;
    sticky.Shapes[0].Surface.Friction = 1;
    sticky.Shapes[0].Surface.FrictionCombine = GPhysics::CombineMode::Maximum;
    Check(world.PutBody(sticky, error), error);
    auto invalid = slippery;
    invalid.AllowedDOFs = 64;
    Check(!world.PutBody(invalid, error) && !error.empty(), "Backend accepted unknown DOF bits");
    GPhysics::BodyState original;
    Check(world.GetBodyState(2, original) && original.Transform.Position.X == -4,
          "Invalid replacement destroyed the original body");
    std::vector<GPhysics::Contact> contacts;
    for (unsigned i = 0; i < 120; ++i)
        Check(world.Step(1.f / 60, contacts, error), error);
    GPhysics::BodyState free, stopped;
    Check(world.GetBodyState(2, free) && world.GetBodyState(3, stopped), "Cannot read body states");
    Check(free.LinearVelocity.X > 3.5f && std::fabs(stopped.LinearVelocity.X) < 0.5f,
          "Actual contacts did not use per-Collider friction/combine rules");
    sticky.Shapes[0].Surface.Friction = std::numeric_limits<float>::max();
    ground.Shapes[0].Surface.Friction = std::numeric_limits<float>::max();
    Check(world.PutBody(sticky, error) && world.PutBody(ground, error), error);
    for (unsigned i = 0; i < 10; ++i)
        Check(world.Step(1.f / 60, contacts, error), error);
    Check(world.GetBodyState(3, stopped) && std::isfinite(stopped.Transform.Position.X) &&
              std::isfinite(stopped.Transform.Position.Y) && std::isfinite(stopped.LinearVelocity.X),
          "Finite large friction produced a NaN/Inf simulation state");
}
void QueryExclusions() {
    std::string error;GPhysics::World world;Check(world.Initialize(error),error);
    GPhysics::BodySettings compound;compound.Key=1;compound.Motion=GPhysics::MotionType::Dynamic;
    GPhysics::Shape first;first.Collider=10;first.Layer=3;first.LocalPose.Position={0,3,0};
    GPhysics::Shape second=first;second.Collider=11;second.LocalPose.Position={0,1,0};compound.Shapes={first,second};
    Check(world.PutBody(compound,error),error);
    GPhysics::BodySettings floor;floor.Key=2;GPhysics::Shape ground=first;ground.Collider=20;ground.LocalPose.Position={0,-1,0};floor.Shapes={ground};
    Check(world.PutBody(floor,error),error);
    GPhysics::BodySettings triggerBody;triggerBody.Key=3;GPhysics::Shape trigger=first;trigger.Collider=30;trigger.Trigger=true;trigger.LocalPose.Position={0,4.5f,0};triggerBody.Shapes={trigger};
    Check(world.PutBody(triggerBody,error),error);
    GPhysics::RayHit hit;
    Check(world.Raycast({0,6,0},{0,-10,0},1u<<3,false,hit)&&hit.Collider==10,"Default query lost dynamic compound leaf");
    Check(world.Raycast({0,6,0},{0,-10,0},1u<<3,false,hit,{10})&&hit.Collider==11,"Ignored leaf removed other compound leaves");
    Check(world.Raycast({0,6,0},{0,-10,0},1u<<3,false,hit,{10,11})&&hit.Collider==20,"Excluded compound leaves hid same-layer independent body");
    Check(world.Raycast({0,6,0},{0,-10,0},1u<<3,true,hit,{10,11})&&hit.Collider==30,"IncludeTriggers changed after exclusions");
    Check(world.Raycast({0,6,0},{0,-10,0},1u<<3,true,hit,{10,11,30})&&hit.Collider==20,"Ignored trigger obstructed raycast");
    Check(!world.Raycast({0,6,0},{0,-10,0},1u<<2,true,hit,{10,11})&&!hit.Collider,"Layer-filtered miss retained hit");
}
void FreezeAll() {
    std::string error;GPhysics::World world;Check(world.Initialize(error),error);
    GPhysics::BodySettings frozen;frozen.Key=1;frozen.Motion=GPhysics::MotionType::Dynamic;
    frozen.AllowedDOFs=0;frozen.Transform.Position={0,3,0};frozen.LinearVelocity={10,20,30};
    frozen.AngularVelocity={1,2,3};GPhysics::Shape shape;shape.Collider=1;frozen.Shapes={shape};
    Check(world.PutBody(frozen,error),error);
    GPhysics::BodySettings fixed;fixed.Key=2;fixed.Transform.Position={0,3,0.9f};
    fixed.Shapes={shape};fixed.Shapes[0].Collider=2;Check(world.PutBody(fixed,error),error);
    Check(world.SetVelocity(1,{50,60,70},{2,3,4})&&world.AddForce(1,{10000,0,0},false)&&
          world.AddForce(1,{10000,0,0},true)&&world.AddTorque(1,{10000,0,0},false),
          "Fully frozen semantic dynamic body rejected finite commands");
    unsigned enters=0;std::vector<GPhysics::Contact> contacts;
    for(unsigned i=0;i<60;++i) {
        Check(world.Step(1.f/60,contacts,error),error);
        for(const auto& contact:contacts)if(contact.State==GPhysics::Contact::Phase::Enter&&!contact.Trigger)++enters;
    }
    GPhysics::BodyState state;
    Check(world.GetBodyState(1,state)&&state.Transform==frozen.Transform&&
          state.LinearVelocity==GPhysics::Vector3{}&&state.AngularVelocity==GPhysics::Vector3{}&&enters==1,
          "FreezeAll moved, retained velocity or lost semantic dynamic/static collision callbacks");
    auto invalid=frozen;invalid.AllowedDOFs=64;
    Check(!world.PutBody(invalid,error)&&world.GetBodyState(1,state)&&state.Transform==frozen.Transform,
          "Invalid replacement damaged a frozen body");
    world.RemoveBody(2);frozen.AllowedDOFs=63;frozen.LinearVelocity={};frozen.AngularVelocity={};frozen.GravityFactor=0;
    Check(world.PutBody(frozen,error),error);
    for(unsigned i=0;i<10;++i)Check(world.Step(1.f/60,contacts,error),error);
    Check(world.GetBodyState(1,state)&&state.Transform.Position==frozen.Transform.Position,
          "Unlock replayed force or velocity from the frozen interval");
    Check(world.AddForce(1,{1,0,0},true),"Unlocked dynamic body rejected a new impulse");
    for(unsigned i=0;i<30;++i)Check(world.Step(1.f/60,contacts,error),error);
    Check(world.GetBodyState(1,state)&&state.Transform.Position.X>0.4f,"Unlock did not restore dynamic motion");
}
} // namespace
int main() {
    try {
        Materials();
        World();
        QueryExclusions();
        FreezeAll();
        std::cout << "PASS material combine priority/numeric limits and actual Jolt friction simulation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
