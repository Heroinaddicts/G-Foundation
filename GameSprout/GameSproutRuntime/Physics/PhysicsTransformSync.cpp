#include "Components/TransformComponent.h"
#include "GameObject.h"
#include "PhysicsConversions.h"
#include "RuntimePhysicsSystem.h"
#include <algorithm>

namespace GameSproutRuntime {
bool RuntimePhysicsSystem::PublishTransforms(SceneWorld &world, std::string &error) {
    std::vector<std::pair<std::size_t, uint64_t>> ordered;
    for (const auto &[id, record] : world.Bodies)
        if (record.ComponentID) {
            auto *object = _Runtime.QueryGameObject(id);
            std::size_t depth = 0;
            for (auto *parent = object ? object->Parent() : nullptr; parent; parent = parent->Parent())
                ++depth;
            ordered.emplace_back(depth, id);
        }
    // Parent world poses must be published before converting a child body's
    // world pose back to local TRS.
    std::sort(ordered.begin(), ordered.end());
    for (const auto &[depth, id] : ordered) {
        auto &record = world.Bodies.at(id);
        auto *rigid = dynamic_cast<RigidbodyComponent *>(ResolveComponent(id, record.ComponentID));
        if (!rigid)
            continue;
        GPhysics::BodyState body;
        if (!world.Backend.GetBodyState(id, body))
            continue;
        auto *object = rigid->GetGameObject();
        auto *transform = dynamic_cast<TransformComponent *>(object->GetTransform());
        Mat4 local = RuntimeMath::ComposeTRS(RuntimeVector(body.Transform.Position),
                                             RuntimeRotation(body.Transform.Rotation), record.WorldScale);
        if (object->Parent()) {
            Mat4 inverse;
            if (!RuntimeMath::TryInverseAffine(object->Parent()->GetTransform()->GetWorldMatrix(), inverse)) {
                error = "Cannot publish physics pose through a singular parent transform";
                return false;
            }
            local = inverse * local;
        }
        if (!transform->SetPhysicsPose(local, rigid->Settings.MotionType == PhysicsMotionType::Dynamic)) {
            error = "Physics result cannot be represented by this node's local TRS";
            return false;
        }
        rigid->LinearVelocity = RuntimeVector(body.LinearVelocity);
        rigid->AngularVelocity = RuntimeVector(body.AngularVelocity);
        rigid->Sleeping = !body.Active;
        record.LastWorld = transform->GetWorldMatrix();
        record.TransformRevision = transform->GetAuthoringRevision();
    }
    return true;
}
} // namespace GameSproutRuntime
