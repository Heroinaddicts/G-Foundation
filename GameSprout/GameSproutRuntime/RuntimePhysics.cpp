#include "GameObject.h"
#include "GameSproutRuntime.h"
#include "Physics/MeshColliderGeometry.h"
#include "Physics/RuntimePhysicsSystem.h"
#include "Scene.h"

namespace GameSproutRuntime {
namespace {
bool CookMeshCollision(const MeshCollisionBakeInput &input, CookedMeshCollisionData &output,
                       std::string &error) {
    output={};
    GPhysics::MeshGeometry geometry;
    if(!DecodeMeshColliderGeometry(input,geometry,error))return false;
    if(geometry.Vertices.empty()) {
        error="Mesh collision cooking requires prepared positions and indices";
        return false;
    }
    std::string unsupported;
    GPhysics::CookMeshArtifact(geometry,false,output.Triangle,unsupported);
    GPhysics::CookMeshArtifact(geometry,true,output.Convex,unsupported);
    return true;
}
}
MeshCollisionCooker Runtime::GetMeshCollisionCooker() const {
    return {&CookMeshCollision,GPhysics::MeshArtifactFormat()};
}
bool Runtime::SynchronizePhysics(std::string& error) {
    error.clear();
    if(IsExecuting()||IsConstructing()||_ShuttingDown) {error="Synchronize physics outside Runtime callbacks and construction";return false;}
    if(!PreparePhysics(error))return false;
    RefreshObservedScriptProperties();DispatchObjectPropertyChanges();return true;
}
bool Runtime::PreparePhysics(std::string &error) {
    if (!_PhysicsSystem)
        _PhysicsSystem = new RuntimePhysicsSystem(*this);
    RuntimeOperation operation(*this);
    return _PhysicsSystem->Prepare(error);
}
void Runtime::StepPhysics(float delta) {
    if (!_ScriptExecutionEnabled)
        return;
    if (!_PhysicsSystem)
        _PhysicsSystem = new RuntimePhysicsSystem(*this);
    std::string error;
    RuntimeOperation operation(*this);
    if (!_PhysicsSystem->Step(delta, error) && error != _LastPhysicsError) {
        _LastPhysicsError = error;
        for (Scene *scene : _Scenes) {
            SceneNodeEvent event;
            event.Type = SceneNodeEventType::OperationFailed;
            event.SceneID = scene->GetInstanceID();
            event.ObjectID = event.SceneID;
            event.ComponentType = "GameSproutRuntime.Physics";
            event.Error = error;
            _Events.push_back({++_EventSequence, std::move(event)});
        }
    } else if (error.empty())
        _LastPhysicsError.clear();
}
void Runtime::ResetPhysics(bool releaseSystem) {
    if (releaseSystem) {
        delete _PhysicsSystem;
        _PhysicsSystem = nullptr;
    } else if (_PhysicsSystem) {
        _PhysicsSystem->Reset();
    }
    _LastPhysicsError.clear();
}
void Runtime::DropPhysicsSubscriptions(uint64_t object, uint64_t component) {
    for (const auto &[id, node] : _Nodes)
        for (const auto &entry : node->Components)
            if (auto *collider = dynamic_cast<PhysicsColliderState *>(entry.Pointer))
                collider->DropSubscriptions(object, component);
}
bool Runtime::Raycast(IScene *scene, const Ray &ray, float distance, PhysicsRaycastHit &hit,
                      const PhysicsQuerySettings &settings, std::string &error) {
    hit = {};
    error.clear();
    if (_ShuttingDown || _ConstructionDepth) {
        error = "Physics queries are unavailable during construction or shutdown";
        return false;
    }
    if (!_PhysicsSystem)
        _PhysicsSystem = new RuntimePhysicsSystem(*this);
    RuntimeOperation operation(*this);
    return _PhysicsSystem->Raycast(scene, ray, distance, hit, settings, error);
}
} // namespace GameSproutRuntime
