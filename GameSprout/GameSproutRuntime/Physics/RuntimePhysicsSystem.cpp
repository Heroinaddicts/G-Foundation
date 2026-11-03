#include "RuntimePhysicsSystem.h"
#include "Components/TransformComponent.h"
#include "GameObject.h"
#include <set>

namespace GameSproutRuntime {
RuntimePhysicsSystem::~RuntimePhysicsSystem() {
    Reset();
}
void RuntimePhysicsSystem::ForgetMissingScenes() {
    std::set<uint64_t> live;
    for (std::size_t i = 0; i < _Runtime.GetSceneCount(); ++i)
        live.insert(_Runtime.GetScene(i)->GetInstanceID());
    for (auto it = _Worlds.begin(); it != _Worlds.end();)
        if (!live.count(it->first)) {
            delete it->second;
            it = _Worlds.erase(it);
        } else
            ++it;
}
bool RuntimePhysicsSystem::Prepare(std::string &error) {
    error.clear();
    ForgetMissingScenes();
    for (std::size_t i = 0; i < _Runtime.GetSceneCount(); ++i) {
        IScene *scene = _Runtime.GetScene(i);
        if (!scene->IsSimulationEnabled())
            continue;
        auto *&world = _Worlds[scene->GetInstanceID()];
        if (!world) {
            world = new SceneWorld;
            if (!world->Backend.Initialize(error)) {
                delete world;
                world = nullptr;
                return false;
            }
        }
        if (!Synchronize(scene, *world, 0, error) || !PublishTransforms(*world, error))
            return false;
    }
    return true;
}
bool RuntimePhysicsSystem::Step(float delta, std::string &error) {
    error.clear();
    ForgetMissingScenes();
    // Step all worlds before entering user code; a callback cannot invalidate
    // a provider/world while another world is still being iterated.
    std::vector<GPhysics::Contact> notifications;
    for (std::size_t i = 0; i < _Runtime.GetSceneCount(); ++i) {
        IScene *scene = _Runtime.GetScene(i);
        if (!scene->IsSimulationEnabled())
            continue;
        auto *&world = _Worlds[scene->GetInstanceID()];
        if (!world) {
            world = new SceneWorld;
            if (!world->Backend.Initialize(error)) {
                delete world;
                world = nullptr;
                return false;
            }
        }
        if (!Synchronize(scene, *world, delta, error))
            return false;
        // A newly changed world still drains backend removals/Exit events once.
        // Stable static-only pairs cannot generate contacts or moving poses.
        if(world->UnchangedStatic)continue;
        std::vector<GPhysics::Contact> events;
        if (!world->Backend.Step(delta, events, error) || !PublishTransforms(*world, error))
            return false;
        notifications.insert(notifications.end(), std::make_move_iterator(events.begin()),
                             std::make_move_iterator(events.end()));
    }
    Dispatch(notifications);
    return true;
}
void RuntimePhysicsSystem::Reset() {
    // An inactive body's backend record may already have been removed. Clear
    // session state on all surviving nodes, including inactive hierarchies.
    std::vector<IGameObject*> pending;
    for (std::size_t i=0;i<_Runtime.GetSceneCount();++i) pending.push_back(_Runtime.GetScene(i));
    while (!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for (std::size_t i=0;i<object->GetChildCount();++i) pending.push_back(object->GetChild(i));
        if (auto* transform=dynamic_cast<TransformComponent*>(object->GetTransform())) transform->ClearPhysicsPose();
        for (std::size_t i=0;i<object->GetComponentCount();++i)
            if (auto* rigid=dynamic_cast<RigidbodyComponent*>(object->GetComponentAt(i))) {
                    rigid->LinearVelocity = {};
                    rigid->AngularVelocity = {};
                    rigid->Force = {};
                    rigid->Impulse = {};
                    rigid->HasTeleport = false;
                    rigid->HasKinematicTarget = false;
                    rigid->Sleeping = false;
                    rigid->WakeRequested = false;
            }
    }
    for (auto& [scene,world]:_Worlds) delete world;
    _Worlds.clear();
    _ColliderKeys.clear();
    _Identities.clear();
    _Materials.clear();
    _MeshSources.clear();
    _MeshShapes.clear();
    _ResourceRevisions.clear();
    _Provider = nullptr;
    _ProviderRevision = 0;
}
} // namespace GameSproutRuntime
