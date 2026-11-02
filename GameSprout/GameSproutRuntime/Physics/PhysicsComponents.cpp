#include "PhysicsComponents.h"
#include "Api/Math/TransformMath.h"
#include "Components/ComponentFactory.h"
#include "Components/ScriptComponent.h"
#include "GameObject.h"
#include <cmath>
#include <limits>

namespace GameSproutRuntime {
namespace {
bool ValidReference(const ResourceReference &reference) {
    if (reference.Guid.empty())
        return reference.LocalID == 0;
    if (reference.Guid.size() != 32 || !reference.LocalID)
        return false;
    for (char c : reference.Guid)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
bool Same(Vector3 a, Vector3 b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
}
bool Same(Quaternion a, Quaternion b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
}
bool StableRotation(Quaternion value, Quaternion &output) {
    const double squared = double(value.X) * value.X + double(value.Y) * value.Y + double(value.Z) * value.Z +
                           double(value.W) * value.W;
    if (std::isfinite(squared) && std::fabs(squared - 1.0) <= 8.0 * std::numeric_limits<float>::epsilon())
        output = value;
    else if (!value.TryNormalize(output))
        return false;
    if (output.W < 0 ||
        (output.W == 0 &&
         (output.Z < 0 || (output.Z == 0 && (output.Y < 0 || (output.Y == 0 && output.X < 0))))))
        output = {-output.X, -output.Y, -output.Z, -output.W};
    return true;
}
Runtime &RuntimeOwner(IColliderComponent *collider) {
    return dynamic_cast<ObjectAccess *>(collider->GetGameObject())->State().Owner;
}
bool Available(IColliderComponent *collider) {
    auto &runtime = RuntimeOwner(collider);
    auto *node = runtime.Find(collider->GetGameObject());
    if (!runtime.IsInScene(node) || !node->IsActiveInHierarchy())
        return false;
    for (const auto &component : node->Components)
        if (component.Pointer == collider)
            return !component.PendingRemove;
    return false;
}
std::pair<uint64_t, uint64_t> SubscriptionOwner(IColliderComponent *collider) {
    auto *script = RuntimeOwner(collider).GetInvokingScript();
    return script ? std::make_pair(script->GetGameObject()->GetInstanceID(), script->GetComponentInstanceID())
                  : std::pair<uint64_t, uint64_t>{};
}
void Invoke(PhysicsColliderState &state, uint64_t token, const std::function<void()> &callback) {
    auto *collider = state.ColliderInterface();
    auto &runtime = RuntimeOwner(collider);
    const auto found = state.SubscriptionOwners.find(token);
    const auto owner =
        found == state.SubscriptionOwners.end() ? std::pair<uint64_t, uint64_t>{} : found->second;
    if (owner.first) {
        auto *node = runtime.Find(owner.first);
        if (!runtime.IsInScene(node) || !node->IsActiveInHierarchy())
            return;
        for (const auto &component : node->Components)
            if (!component.PendingRemove && component.Pointer->GetComponentInstanceID() == owner.second)
                if (auto *script = dynamic_cast<ScriptComponent *>(component.Pointer)) {
                    script->InvokeExternal(callback);
                    return;
                }
        return;
    }
    try {
        callback();
    } catch (const std::exception &error) {
        runtime.NotifyScriptError(collider->GetGameObject(),
                                  std::string("Physics subscriber failed: ") + error.what());
    } catch (...) {
        runtime.NotifyScriptError(collider->GetGameObject(), "Physics subscriber failed");
    }
}
} // namespace
uint64_t PhysicsColliderState::RegisterCollision(unsigned phase,
                                                 const IColliderComponent::fCollisionCallback &callback) {
    if (!callback || NextSubscription == std::numeric_limits<uint64_t>::max())
        return 0;
    const auto id = ++NextSubscription;
    CollisionCallbacks[phase].emplace(id, callback);
    SubscriptionOwners[id] = SubscriptionOwner(ColliderInterface());
    return id;
}
uint64_t PhysicsColliderState::RegisterTrigger(unsigned phase,
                                               const IColliderComponent::fTriggerCallback &callback) {
    if (!callback || NextSubscription == std::numeric_limits<uint64_t>::max())
        return 0;
    const auto id = ++NextSubscription;
    TriggerCallbacks[phase].emplace(id, callback);
    SubscriptionOwners[id] = SubscriptionOwner(ColliderInterface());
    return id;
}
bool PhysicsColliderState::UnregisterCollision(unsigned phase, uint64_t token) {
    if (!CollisionCallbacks[phase].erase(token))
        return false;
    SubscriptionOwners.erase(token);
    return true;
}
bool PhysicsColliderState::UnregisterTrigger(unsigned phase, uint64_t token) {
    if (!TriggerCallbacks[phase].erase(token))
        return false;
    SubscriptionOwners.erase(token);
    return true;
}
void PhysicsColliderState::DropSubscriptions(uint64_t object, uint64_t component) {
    for (auto it = SubscriptionOwners.begin(); it != SubscriptionOwners.end();)
        if (it->second == std::make_pair(object, component)) {
            for (auto &callbacks : CollisionCallbacks)
                callbacks.erase(it->first);
            for (auto &callbacks : TriggerCallbacks)
                callbacks.erase(it->first);
            it = SubscriptionOwners.erase(it);
        } else
            ++it;
}
void PhysicsColliderState::Dispatch(unsigned phase, const CollisionEvent &event) {
    const auto last = NextSubscription;
    uint64_t previous = 0;
    while (Available(ColliderInterface())) {
        const auto next = CollisionCallbacks[phase].upper_bound(previous);
        if (next == CollisionCallbacks[phase].end() || next->first > last)
            break;
        previous = next->first;
        auto callback = next->second;
        Invoke(*this, previous, [&] { callback(event); });
    }
}
void PhysicsColliderState::Dispatch(unsigned phase, const TriggerEvent &event) {
    const auto last = NextSubscription;
    uint64_t previous = 0;
    while (Available(ColliderInterface())) {
        const auto next = TriggerCallbacks[phase].upper_bound(previous);
        if (next == TriggerCallbacks[phase].end() || next->first > last)
            break;
        previous = next->first;
        auto callback = next->second;
        Invoke(*this, previous, [&] { callback(event); });
    }
}
void PhysicsColliderState::ConfigurationChanged() {
    ++Revision;
    NotifyComponentChanged(ColliderInterface()->GetGameObject(), ColliderInterface());
}
template <typename Interface>
bool ColliderComponentBase<Interface>::SetColliderSettings(const ColliderSettings &input) {
    ColliderSettings value = input;
    if (!RuntimeMath::IsFinite(value.Center) || !StableRotation(value.Rotation, value.Rotation) ||
        value.CollisionLayer > 31 || !ValidReference(value.Material))
        return false;
    if (Settings.Enabled == value.Enabled && Settings.IsTrigger == value.IsTrigger &&
        Same(Settings.Center, value.Center) && Same(Settings.Rotation, value.Rotation) &&
        Settings.Material == value.Material && Settings.CollisionLayer == value.CollisionLayer &&
        Settings.CollisionMask == value.CollisionMask)
        return true;
    Settings = value;
    ConfigurationChanged();
    return true;
}
template <typename Interface>
IRigidbodyComponent *ColliderComponentBase<Interface>::GetAttachedRigidbody() const {
    for (IGameObject *object = this->_GameObject; object; object = object->Parent())
        if (auto *body = object->GetComponent<IRigidbodyComponent>())
            return body->GetSettings().Enabled ? body : nullptr;
    return nullptr;
}
template class ColliderComponentBase<IBoxColliderComponent>;
template class ColliderComponentBase<ISphereColliderComponent>;
template class ColliderComponentBase<ICapsuleColliderComponent>;
bool BoxColliderComponent::SetShape(const BoxColliderShape &shape) {
    if (!RuntimeMath::IsFinite(shape.Size) || shape.Size.X <= 0 || shape.Size.Y <= 0 || shape.Size.Z <= 0)
        return false;
    if (!Same(Shape.Size, shape.Size)) {
        Shape = shape;
        ConfigurationChanged();
    }
    return true;
}
bool SphereColliderComponent::SetShape(const SphereColliderShape &shape) {
    if (!std::isfinite(shape.Radius) || shape.Radius <= 0)
        return false;
    if (Shape.Radius != shape.Radius) {
        Shape = shape;
        ConfigurationChanged();
    }
    return true;
}
bool CapsuleColliderComponent::SetShape(const CapsuleColliderShape &shape) {
    if (!std::isfinite(shape.Radius) || !std::isfinite(shape.Height) || shape.Radius <= 0 ||
        shape.Height < 2 * shape.Radius || shape.Axis > PhysicsAxis::Z)
        return false;
    if (Shape.Radius != shape.Radius || Shape.Height != shape.Height || Shape.Axis != shape.Axis) {
        Shape = shape;
        ConfigurationChanged();
    }
    return true;
}
bool RigidbodyComponent::SetSettings(const RigidbodySettings &value) {
    if (value.MotionType > PhysicsMotionType::Kinematic || !std::isfinite(value.Mass) || value.Mass <= 0 ||
        !std::isfinite(value.GravityFactor) || !std::isfinite(value.LinearDamping) ||
        value.LinearDamping < 0 || !std::isfinite(value.AngularDamping) || value.AngularDamping < 0 ||
        (value.Constraints & ~63u) || value.Constraints == 63u)
        return false;
    if (Settings.Enabled == value.Enabled && Settings.MotionType == value.MotionType &&
        Settings.Mass == value.Mass && Settings.GravityFactor == value.GravityFactor &&
        Settings.LinearDamping == value.LinearDamping && Settings.AngularDamping == value.AngularDamping &&
        Settings.Constraints == value.Constraints && Settings.Continuous == value.Continuous)
        return true;
    Settings = value;
    ++Revision;
    NotifyComponentChanged(_GameObject, this);
    return true;
}
bool RigidbodyComponent::SetLinearVelocity(Vector3 value) {
    if (!RuntimeMath::IsFinite(value))
        return false;
    LinearVelocity = value;
    ++VelocityRevision;
    return true;
}
bool RigidbodyComponent::SetAngularVelocity(Vector3 value) {
    if (!RuntimeMath::IsFinite(value))
        return false;
    AngularVelocity = value;
    ++VelocityRevision;
    return true;
}
bool RigidbodyComponent::AddForce(Vector3 value) {
    const auto sum = Force + value;
    if (!Settings.Enabled || Settings.MotionType != PhysicsMotionType::Dynamic ||
        !RuntimeMath::IsFinite(value) || !RuntimeMath::IsFinite(sum))
        return false;
    Force = sum;
    return true;
}
bool RigidbodyComponent::AddImpulse(Vector3 value) {
    const auto sum = Impulse + value;
    if (!Settings.Enabled || Settings.MotionType != PhysicsMotionType::Dynamic ||
        !RuntimeMath::IsFinite(value) || !RuntimeMath::IsFinite(sum))
        return false;
    Impulse = sum;
    return true;
}
bool RigidbodyComponent::MoveKinematic(Vector3 position, Quaternion rotation) {
    if (!Settings.Enabled || Settings.MotionType != PhysicsMotionType::Kinematic ||
        !RuntimeMath::IsFinite(position) || !rotation.TryNormalize(rotation))
        return false;
    TargetPosition = position;
    TargetRotation = rotation;
    HasKinematicTarget = true;
    return true;
}
bool RigidbodyComponent::Teleport(Vector3 position, Quaternion rotation, bool clear) {
    if (!RuntimeMath::IsFinite(position) || !rotation.TryNormalize(rotation))
        return false;
    TargetPosition = position;
    TargetRotation = rotation;
    HasTeleport = true;
    HasKinematicTarget = false;
    ClearTeleportVelocity = clear;
    return true;
}
IComponent *CreatePhysicsComponent(const std::type_info &type, IGameObject *owner) {
    if (!owner || owner == owner->GetScene())
        return nullptr;
    if (type == typeid(IRigidbodyComponent))
        return new RigidbodyComponent(owner);
    if (type == typeid(IBoxColliderComponent))
        return new BoxColliderComponent(owner);
    if (type == typeid(ISphereColliderComponent))
        return new SphereColliderComponent(owner);
    if (type == typeid(ICapsuleColliderComponent))
        return new CapsuleColliderComponent(owner);
    return nullptr;
}
} // namespace GameSproutRuntime
