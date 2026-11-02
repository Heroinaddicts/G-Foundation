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
    invalid.AllowedDOFs = 0;
    Check(!world.PutBody(invalid, error) && !error.empty(), "Backend accepted unsupported freeze-all DOFs");
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
} // namespace
int main() {
    try {
        Materials();
        World();
        std::cout << "PASS material combine priority/numeric limits and actual Jolt friction simulation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
