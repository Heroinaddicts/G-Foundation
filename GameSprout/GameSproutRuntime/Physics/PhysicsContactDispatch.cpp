#include "GameObject.h"
#include "PhysicsConversions.h"
#include "RuntimePhysicsSystem.h"

namespace GameSproutRuntime {
PhysicsColliderState *RuntimePhysicsSystem::ResolveCollider(const ColliderIdentity &identity) const {
    auto *component = ResolveComponent(identity.ObjectID, identity.ComponentID);
    auto *collider = dynamic_cast<PhysicsColliderState *>(component);
    auto *object = component ? component->GetGameObject() : nullptr;
    if (!collider || !object->GetScene() || object->GetScene()->GetInstanceID() != identity.SceneID ||
        !object->GetScene()->IsSimulationEnabled() || !collider->Settings.Enabled)
        return nullptr;
    return collider;
}
void RuntimePhysicsSystem::Dispatch(const std::vector<GPhysics::Contact> &events) {
    // This scope also covers every subscriber. Destruction and reparenting are
    // committed only once the whole event batch has released its borrowed data.
    RuntimeOperation operation(_Runtime);
    for (const auto &contact : events) {
        const auto first = _Identities.find(contact.FirstCollider),
                   second = _Identities.find(contact.SecondCollider);
        if (first == _Identities.end() || second == _Identities.end())
            continue;
        const ColliderIdentity identities[2] = {first->second, second->second};
        for (unsigned side = 0; side < 2; ++side) {
            auto *collider = ResolveCollider(identities[side]);
            if (!collider)
                continue;
            const auto phase = static_cast<unsigned>(contact.State);
            if (contact.Trigger) {
                TriggerEvent event;
                event.Self = identities[side];
                event.Other = identities[1 - side];
                collider->Dispatch(phase, event);
            } else {
                CollisionEvent event;
                event.Self = identities[side];
                event.Other = identities[1 - side];
                const Vector3 normal = RuntimeVector(contact.Normal) * (side == 0 ? -1.0f : 1.0f);
                for (const auto &point : contact.Points)
                    event.Contacts.push_back({RuntimeVector(point), normal, -contact.Penetration});
                auto *selfBody = collider->ColliderInterface()->GetAttachedRigidbody();
                auto *otherCollider = ResolveCollider(identities[1 - side]);
                auto *otherBody =
                    otherCollider ? otherCollider->ColliderInterface()->GetAttachedRigidbody() : nullptr;
                event.RelativeVelocity = (selfBody ? selfBody->GetLinearVelocity() : Vector3{}) -
                                         (otherBody ? otherBody->GetLinearVelocity() : Vector3{});
                collider->Dispatch(phase, event);
            }
        }
    }
}
} // namespace GameSproutRuntime
