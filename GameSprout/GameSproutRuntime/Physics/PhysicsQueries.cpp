#include "GameObject.h"
#include "PhysicsConversions.h"
#include "RuntimePhysicsSystem.h"
#include <cmath>

namespace GameSproutRuntime {
bool RuntimePhysicsSystem::Raycast(IScene *scene, const Ray &ray, float maximum, PhysicsRaycastHit &output,
                                   const PhysicsQuerySettings &settings, std::string &error) {
    error.clear();
    output = {};
    auto *state = _Runtime.Find(scene);
    if (!_Runtime.IsInScene(state) || !state->IsSceneRoot() || !RuntimeMath::IsFinite(ray.Origin) ||
        !RuntimeMath::IsFinite(ray.Direction) || !std::isfinite(maximum) || maximum <= 0) {
        error = "Raycast requires a live scene, finite ray and positive maximum distance";
        return false;
    }
    const double length =
        std::sqrt(double(ray.Direction.X) * ray.Direction.X + double(ray.Direction.Y) * ray.Direction.Y +
                  double(ray.Direction.Z) * ray.Direction.Z);
    if (!std::isfinite(length) || length == 0) {
        error = "Raycast direction must be nonzero";
        return false;
    }
    const Vector3 displacement{
        static_cast<float>(double(ray.Direction.X) / length * maximum),
        static_cast<float>(double(ray.Direction.Y) / length * maximum),
        static_cast<float>(double(ray.Direction.Z) / length * maximum)};
    if (!RuntimeMath::IsFinite(displacement)) {
        error = "Raycast displacement is too large";
        return false;
    }
    auto *&world = _Worlds[scene->GetInstanceID()];
    if (!world) {
        world = new SceneWorld;
        if (!world->Backend.Initialize(error)) {
            delete world;
            world = nullptr;
            return false;
        }
    }
    // Queries in edit mode synchronize geometry, but neither integrate time nor
    // publish simulated poses or collision notifications.
    if (!Synchronize(scene, *world, 0, error))
        return false;
    GPhysics::RayHit hit;
    if (!world->Backend.Raycast(PhysicsVector(ray.Origin), PhysicsVector(displacement),
                                settings.CollisionMask, settings.IncludeTriggers, hit))
        return false;
    const auto identity = _Identities.find(hit.Collider);
    if (identity == _Identities.end())
        return false;
    output.Collider = identity->second;
    output.Position = RuntimeVector(hit.Position);
    output.Normal = RuntimeVector(hit.Normal);
    output.Distance = hit.Fraction * maximum;
    return true;
}
} // namespace GameSproutRuntime
