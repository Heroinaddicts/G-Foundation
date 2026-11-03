#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void Steps(GameSproutRuntime::IGameSproutRuntime &runtime, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        runtime.FixedUpdate(1.0f / 60.0f);
}
class Materials final : public GameSproutRuntime::IRuntimeResourceProvider {
  public:
    unsigned Reads = 0;
    uint64_t GetRevision() const override {
        return 1;
    }
    bool LoadMesh(const GameSproutRuntime::ResourceReference &, GameSproutRuntime::MeshRenderData &,
                  std::string &) override {
        return false;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference &, GameSproutRuntime::TextureRenderData &,
                     std::string &) override {
        return false;
    }
    bool LoadMaterial(const GameSproutRuntime::ResourceReference &, GameSproutRuntime::MaterialRenderData &,
                      std::string &) override {
        return false;
    }
    bool LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference &reference,
                             GameSproutRuntime::PhysicsMaterialData &value, std::string &error) override {
        ++Reads;
        error.clear();
        value = {};
        value.Friction = reference.LocalID == 1 ? 0.1f : 0.9f;
        value.FrictionCombine = GameSproutRuntime::PhysicsMaterialCombine::Maximum;
        return true;
    }
};
GameSproutRuntime::IBoxColliderComponent *Ground(GameSproutRuntime::IScene *scene) {
    auto *object = scene->CreateGameObject("Ground");
    object->GetTransform()->SetLocalPosition({0, -0.5f, 0});
    auto *collider = object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(collider && collider->SetShape({{30, 1, 30}}), "Cannot create a ground collider");
    return collider;
}
void CheckUnityWorldDirections(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto* scene=runtime.CreateScene("Unity world physics directions");
    auto* wall=scene->CreateGameObject("Positive Z wall");
    wall->GetTransform()->SetLocalPosition({0,0,5});
    Check(wall->AddComponent<GameSproutRuntime::IBoxColliderComponent>(),"Cannot create +Z wall");
    GameSproutRuntime::PhysicsRaycastHit hit;GameSproutRuntime::PhysicsQuerySettings query;
    Check(runtime.Raycast(scene,{{0,0,0},{0,0,1}},10,hit,query,error)&&
          hit.Collider.ObjectID==wall->GetInstanceID()&&std::fabs(hit.Distance-4.5f)<.001f&&
          std::fabs(hit.Position.Z-4.5f)<.001f&&hit.Normal.Z<-.99f,
          "+Z world ray/position/normal changed sign at the physics boundary: "+error);
    auto* actor=scene->CreateGameObject("Positive Y turn");
    actor->GetTransform()->SetLocalPosition({-4,2,0});
    const float halfTurn=std::sqrt(.5f);
    Check(actor->GetTransform()->SetLocalRotation({0,halfTurn,0,halfTurn}),"Cannot author +90 Y rotation");
    Check(actor->AddComponent<GameSproutRuntime::ISphereColliderComponent>(),"Cannot create rotating sphere");
    auto* body=actor->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto settings=body->GetSettings();settings.GravityFactor=0;settings.LinearDamping=0;settings.AngularDamping=0;
    Check(body->SetSettings(settings)&&runtime.BeginExecution(error),error);
    runtime.FixedUpdate(1.f/60);
    auto matrix=actor->GetTransform()->GetWorldMatrix();
    Check(matrix(0,2)>.999f&&std::fabs(matrix(2,2))<.001f,
          "Authored +90 Y quaternion did not rotate local +Z toward +X through Jolt");
    Check(runtime.EndExecution(error),error);
    Check(actor->GetTransform()->SetLocalRotation({}),"Cannot reset authored rotation");
    Check(runtime.BeginExecution(error),error);
    Check(body->SetLinearVelocity({0,0,2})&&body->SetAngularVelocity({0,GameSproutRuntime::Pi*.5f,0}),
          "Cannot set Unity-style velocity directions");
    Steps(runtime,30);matrix=actor->GetTransform()->GetWorldMatrix();
    Check(std::fabs(matrix(2,3)-1)<.002f&&matrix(0,2)>.69f&&matrix(0,2)<.72f&&
          matrix(2,2)>.69f&&matrix(2,2)<.72f,
          "+Z velocity or +Y angular velocity was reflected during simulation");
    Check(body->GetLinearVelocity().Z>1.99f&&body->GetAngularVelocity().Y>1.56f,
          "Physics velocity getters changed the public world convention");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),error);
}
void CheckQueriesAndStaticPairs(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    auto *scene = runtime.CreateScene("Static colliders");
    auto *first = scene->CreateGameObject("First")->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto *second =
        scene->CreateGameObject("Second")->AddComponent<GameSproutRuntime::ISphereColliderComponent>();
    unsigned events = 0;
    first->RegisterCollisionEnter([&](const auto &) { ++events; });
    second->RegisterCollisionEnter([&](const auto &) { ++events; });
    GameSproutRuntime::PhysicsRaycastHit hit;
    GameSproutRuntime::PhysicsQuerySettings settings;
    Check(runtime.Raycast(scene, {{0, 3, 0}, {0, -2, 0}}, 10, hit, settings, error), error);
    Check(hit.Collider.SceneID == scene->GetInstanceID() && hit.Collider.ObjectID &&
              hit.Collider.ComponentID && std::fabs(hit.Distance - 2.5f) < 0.01f,
          "Edit raycast did not hit static geometry with stable identity/distance");
    Check(first->GetGameObject()->GetTransform()->GetLocalPosition().Y == 0, "Edit raycast advanced physics");
    Check(runtime.Raycast(scene, {{0,3,0},{0,-std::numeric_limits<float>::denorm_min(),0}},10,hit,settings,error),
          "Finite nonzero subnormal Raycast direction overflowed normalization");
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 5);
    Check(events == 0, "Two static colliders generated simulation callbacks");
    Check(runtime.EndExecution(error), error);
    auto configuration = second->GetColliderSettings();
    configuration.IsTrigger = true;
    Check(second->SetColliderSettings(configuration), "Cannot configure static trigger");
    second->RegisterTriggerEnter([&](const auto &) { ++events; });
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 5);
    Check(events == 0, "Static trigger vs static collider generated a callback");
    Check(runtime.EndExecution(error), error);
    Check(!runtime.Raycast(scene, {{0, 3, 0}, {0, 0, 0}}, 10, hit, settings, error) && !error.empty(),
          "Zero direction was accepted");
    Check(runtime.DestroyScene(scene), "Cannot destroy static fixture");
}
void CheckQueryObjectExclusions(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto* scene=runtime.CreateScene("Raycast object exclusions");
    auto* ground=Ground(scene);
    auto* player=scene->CreateGameObject("Player");player->GetTransform()->SetLocalPosition({0,3,0});
    auto* capsule=player->AddComponent<GameSproutRuntime::ICapsuleColliderComponent>();
    auto* body=player->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto* child=scene->CreateGameObject("Compound child",player);child->GetTransform()->SetLocalPosition({0,1.5f,0});
    auto* childCollider=child->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto* triggerObject=scene->CreateGameObject("Floating trigger");triggerObject->GetTransform()->SetLocalPosition({0,5.5f,0});
    auto* trigger=triggerObject->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(capsule&&body&&childCollider&&trigger&&trigger->SetShape({{.4f,.4f,.4f}}),"Cannot create raycast exclusions fixture");
    auto triggerSettings=trigger->GetColliderSettings();triggerSettings.IsTrigger=true;
    Check(trigger->SetColliderSettings(triggerSettings),"Cannot enable exclusion fixture trigger");
    const GameSproutRuntime::Ray ray{{0,6,0},{0,-1,0}};
    GameSproutRuntime::PhysicsRaycastHit hit;GameSproutRuntime::PhysicsQuerySettings query;
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==child->GetInstanceID(),
          "Default raycast did not find nearest non-trigger compound child");
    query.IgnoredObjectIDs={child->GetInstanceID()};
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==player->GetInstanceID(),
          "Ignoring one child wrongly suppressed sibling Collider on same compound body");
    query.IgnoredObjectIDs={player->GetInstanceID()};
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==ground->GetGameObject()->GetInstanceID()&&std::fabs(hit.Distance-6)<.01f,
          "Self exclusion did not cover all compound colliders or suppressed another object on same Layer");
    auto* otherScene=runtime.CreateScene("Other query scene");auto* foreign=otherScene->CreateGameObject("Foreign");
    query.IgnoredObjectIDs={0,player->GetInstanceID(),player->GetInstanceID(),foreign->GetInstanceID(),std::numeric_limits<uint64_t>::max()};
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==ground->GetGameObject()->GetInstanceID(),
          "Duplicate/stale/foreign object exclusions changed the query");
    query.IncludeTriggers=true;
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==triggerObject->GetInstanceID(),
          "Object exclusions changed IncludeTriggers filtering");
    query.IgnoredObjectIDs.push_back(triggerObject->GetInstanceID());
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==ground->GetGameObject()->GetInstanceID(),
          "Ignored trigger remained a raycast obstruction");
    query.IncludeTriggers=false;
    auto* independent=scene->CreateGameObject("Independent child body",player);independent->GetTransform()->SetLocalPosition({0,2,0});
    Check(independent->AddComponent<GameSproutRuntime::IBoxColliderComponent>()&&independent->AddComponent<GameSproutRuntime::IRigidbodyComponent>(),"Cannot create independent descendant body");
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==independent->GetInstanceID(),
          "Ignoring Rigidbody owner recursively excluded an independent descendant body");
    query.IgnoredObjectIDs.push_back(independent->GetInstanceID());
    Check(runtime.BeginExecution(error),error);
    Check(runtime.Raycast(scene,ray,10,hit,query,error)&&hit.Collider.ObjectID==ground->GetGameObject()->GetInstanceID(),
          "Self exclusions stopped working in gameplay mode");
    Check(player->GetTransform()->GetLocalPosition().Y==3,"Raycast exclusion advanced dynamic simulation");
    query.CollisionMask=0;error="old error";
    Check(!runtime.Raycast(scene,ray,10,hit,query,error)&&error.empty()&&!hit.Collider.ObjectID,
          "Filtered miss did not clear hit/error or ignored the layer mask");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene)&&runtime.DestroyScene(otherScene),error);
}
void CheckFallingAndCallbackSafety(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    auto *scene = runtime.CreateScene("Falling boxes");
    auto *ground = Ground(scene);
    auto *object = scene->CreateGameObject("Box");
    object->GetTransform()->SetLocalPosition({0, 4, 0});
    auto *collider = object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto *body = object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    Check(collider && body, "Cannot create dynamic box");
    const auto thread = std::this_thread::get_id();
    unsigned enter = 0, stay = 0, exit = 0, late = 0;
    bool identity = false, normal = false, correctThread = true;
    uint64_t later = 0;
    collider->RegisterCollisionEnter(
        [&](const auto &) { throw std::runtime_error("Deliberate host subscriber failure"); });
    collider->RegisterCollisionEnter([&](const auto &event) {
        ++enter;
        identity = event.Self.ComponentID == collider->GetComponentInstanceID() &&
                   event.Other.ComponentID == ground->GetComponentInstanceID();
        normal = !event.Contacts.empty() && event.Contacts[0].Normal.Y > 0.5f;
        correctThread = correctThread && std::this_thread::get_id() == thread;
        collider->UnregisterCollisionEnter(later);
    });
    later = collider->RegisterCollisionEnter([&](const auto &) { ++late; });
    collider->RegisterCollisionStay([&](const auto &) { ++stay; });
    collider->RegisterCollisionExit([&](const auto &) { ++exit; });
    runtime.Update(1);
    runtime.FixedUpdate(0.5f);
    Check(object->GetTransform()->GetLocalPosition().Y == 4, "Editing automatically simulated gravity");
    auto invalid = body->GetSettings();
    invalid.Constraints = 64;
    Check(!body->SetSettings(invalid), "Unknown constraint bits were accepted");
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 150);
    const auto settled = object->GetTransform()->GetLocalPosition();
    Check(settled.Y > 0.45f && settled.Y < 0.55f, "Dynamic box did not settle on static ground");
    Check(enter == 1 && stay > 0 && exit == 0 && late == 0 && identity && normal && correctThread,
          "Collider identity, event aggregation, normal, thread or immediate unsubscribe failed");
    Steps(runtime, 150);
    Check(exit == 0, "Sleeping generated a false CollisionExit");
    Check(body->IsSleeping(), "A resting body was repeatedly woken by empty force commands");
    Check(body->Teleport({0, 4, 0}, {}, true), "Cannot teleport dynamic body");
    Steps(runtime, 2);
    Check(exit == 1, "Leaving a contact did not produce exactly one Exit");
    Check(runtime.EndExecution(error), error);
    Check(object->GetTransform()->GetLocalPosition().Y == 4,
          "Ending gameplay did not restore authored transform");
    Check(runtime.BeginExecution(error), error);
    auto config = collider->GetColliderSettings();
    config.Enabled = false;
    Check(collider->SetColliderSettings(config), "Cannot disable collider");
    Steps(runtime, 120);
    Check(object->GetTransform()->GetLocalPosition().Y < -5,
          "Disabling collision also disabled rigidbody gravity");
    Check(runtime.EndExecution(error), error);
    Check(runtime.DestroyScene(scene), "Cannot destroy falling fixture");
}
void CheckTriggersAndRemoval(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    auto *scene = runtime.CreateScene("Triggers");
    auto *zoneObject = scene->CreateGameObject("Trigger");
    auto *zone = zoneObject->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(zone->SetShape({{4, 4, 4}}), "Cannot set trigger size");
    auto settings = zone->GetColliderSettings();
    settings.IsTrigger = true;
    Check(zone->SetColliderSettings(settings), "Cannot enable Trigger");
    auto *object = scene->CreateGameObject("Kinematic");
    object->GetTransform()->SetLocalPosition({6, 0, 0});
    auto *body = object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto bodySettings = body->GetSettings();
    bodySettings.MotionType = GameSproutRuntime::PhysicsMotionType::Kinematic;
    Check(body->SetSettings(bodySettings), "Cannot enable Kinematic");
    auto *collider = object->AddComponent<GameSproutRuntime::ISphereColliderComponent>();
    unsigned enter = 0, stay = 0, exit = 0, collision = 0;
    zone->RegisterTriggerEnter([&](const auto &event) {
        ++enter;
        Check(event.Other.ObjectID == object->GetInstanceID(), "Trigger did not identify kinematic collider");
    });
    zone->RegisterTriggerStay([&](const auto &) { ++stay; });
    zone->RegisterTriggerExit([&](const auto &) { ++exit; });
    zone->RegisterCollisionEnter([&](const auto &) { ++collision; });
    Check(runtime.BeginExecution(error), error);
    Check(body->MoveKinematic({0, 0, 0}, {}), "Cannot move Kinematic");
    Steps(runtime, 4);
    Check(enter == 1 && stay > 0 && collision == 0, "Kinematic trigger events were missing or duplicated");
    Check(body->MoveKinematic({6, 0, 0}, {}), "Cannot leave trigger");
    Steps(runtime, 3);
    Check(exit == 1, "Trigger exit was missing or duplicated");
    unsigned first = 0, second = 0;
    zone->RegisterTriggerEnter([&](const auto &) {
        ++first;
        zoneObject->RemoveComponentInstance(zone->GetComponentInstanceID());
    });
    zone->RegisterTriggerEnter([&](const auto &) { ++second; });
    Check(body->MoveKinematic({0, 0, 0}, {}), "Cannot reenter trigger");
    Steps(runtime, 3);
    Check(first == 1 && second == 0 &&
              zoneObject->GetComponents<GameSproutRuntime::IColliderComponent>().empty(),
          "Removing collider during callback retained or dereferenced it");
    Check(runtime.EndExecution(error) && runtime.DestroyScene(scene), "Cannot clean Trigger fixture");
}
void CheckCompoundAndWorldIsolation(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    Materials materials;
    runtime.SetResourceProvider(&materials);
    auto *scene = runtime.CreateScene("Compound");
    Ground(scene);
    auto *root = scene->CreateGameObject("Compound body");
    root->GetTransform()->SetLocalPosition({0, 3, 0});
    auto *body = root->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    unsigned enters[2] = {0, 0};
    for (unsigned i = 0; i < 2; ++i) {
        auto *child = scene->CreateGameObject("Part", root);
        child->GetTransform()->SetLocalPosition({i ? 1.0f : -1.0f, 0, 0});
        auto *collider = child->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto settings = collider->GetColliderSettings();
        settings.Material = {"0123456789abcdef0123456789abcdef", i + 1};
        Check(collider->SetColliderSettings(settings), "Cannot assign per-Collider material");
        Check(collider->GetAttachedRigidbody() == body, "Child collider did not attach to parent rigidbody");
        collider->RegisterCollisionEnter([&, i](const auto &) { ++enters[i]; });
    }
    auto *frozen = runtime.CreateScene("Frozen world");
    auto *frozenObject = frozen->CreateGameObject("Frozen body");
    frozenObject->GetTransform()->SetLocalPosition({0, 3, 0});
    frozenObject->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    frozen->SetSimulationEnabled(false);
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 120);
    Check(enters[0] > 0 && enters[1] > 0,
          "Compound child Collider events were merged into one shape identity");
    Check(materials.Reads == 2, "Material resources were reloaded every fixed tick");
    Check(frozenObject->GetTransform()->GetLocalPosition().Y == 3, "Frozen scene physics advanced");
    Check(runtime.EndExecution(error), error);
    // A disabled nearest body is a static boundary, not an invitation to bind
    // its colliders to an enabled grandparent body.
    auto *intermediate = scene->CreateGameObject("Disabled body", root);
    auto *disabled = intermediate->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto settings = disabled->GetSettings();
    settings.Enabled = false;
    Check(disabled->SetSettings(settings), "Cannot disable nested body");
    auto *descendant = scene->CreateGameObject("Static descendant", intermediate);
    auto *collider = descendant->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(!collider->GetAttachedRigidbody(), "Disabled nearest Rigidbody was bypassed");
    Check(runtime.DestroyScene(scene) && runtime.DestroyScene(frozen), "Cannot clean compound fixture");
    runtime.SetResourceProvider(nullptr);
}
void CheckRigidbodyWithoutColliderAndParents(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    auto *scene = runtime.CreateScene("Body only");
    auto *parent = scene->CreateGameObject("Parent");
    parent->GetTransform()->SetLocalPosition({10, 0, 0});
    auto *object = scene->CreateGameObject("Body without geometry", parent);
    object->GetTransform()->SetLocalPosition({0, 4, 0});
    auto *body = object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    Check(body->AddImpulse({0, 2, 0}), "Cannot queue an impulse before startup");
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 90);
    Check(object->GetTransform()->GetLocalPosition().Y < 0 &&
              std::fabs(object->GetTransform()->GetLocalPosition().X) < 0.001f,
          "Body-only gravity or world-to-parent transform conversion failed");
    Check(body->GetLinearVelocity().Y < 0,"Body-only fixture never acquired simulated velocity");
    object->SetActive(false);Steps(runtime,1);
    Check(body->AddForce({0,1,0}),"Cannot retain inactive body force before stop");
    Check(runtime.EndExecution(error),error);
    Check(body->GetLinearVelocity().Y==0&&body->GetAngularVelocity().Y==0,
          "Inactive body retained velocity across gameplay sessions");
    object->SetActive(true);
    Check(runtime.BeginExecution(error),error);
    Check(body->GetLinearVelocity().Y==0,"Restart reused inactive body session state");
    Check(runtime.EndExecution(error) && runtime.DestroyScene(scene), "Cannot clean Body-only fixture");
}
void CheckPublicRigidbodyPropertiesAndFreeze(GameSproutRuntime::IGameSproutRuntime &runtime) {
    std::string error;
    auto* scene=runtime.CreateScene("Public Rigidbody properties");
    auto* object=scene->CreateGameObject("Controlled body");
    object->GetTransform()->SetLocalPosition({0,2,0});
    auto* collider=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    Check(body->SetMass(3)&&body->GetMass()==3&&body->SetGravityFactor(0)&&body->GetGravityFactor()==0&&
          body->SetLinearDamping(0)&&body->GetLinearDamping()==0&&body->SetAngularDamping(0)&&
          body->GetAngularDamping()==0&&body->SetContinuous(true)&&body->IsContinuous(),
          "Public Rigidbody properties are inaccessible");
    Check(body->SetMotionType(GameSproutRuntime::PhysicsMotionType::Kinematic)&&
          body->GetMotionType()==GameSproutRuntime::PhysicsMotionType::Kinematic&&
          body->SetMotionType(GameSproutRuntime::PhysicsMotionType::Dynamic),"Cannot change motion through public API");
    Check(body->SetEnabled(false)&&!body->IsEnabled()&&!collider->GetAttachedRigidbody()&&
          body->SetEnabled(true)&&collider->GetAttachedRigidbody()==body,"Public enabled state is not a Rigidbody boundary");
    Check(!body->SetMass(0)&&!body->SetLinearDamping(-1)&&
          !body->SetAngularDamping(std::numeric_limits<float>::quiet_NaN())&&
          !body->SetMotionType(static_cast<GameSproutRuntime::PhysicsMotionType>(255))&&
          body->GetMass()==3&&body->GetLinearDamping()==0&&body->GetAngularDamping()==0&&body->IsContinuous(),
          "Invalid individual settings overwrote current properties");
    Check(body->SetPositionFrozen(GameSproutRuntime::PhysicsAxis::X,true)&&
          body->SetRotationFrozen(GameSproutRuntime::PhysicsAxis::Y,true)&&
          body->IsPositionFrozen(GameSproutRuntime::PhysicsAxis::X)&&
          !body->IsPositionFrozen(GameSproutRuntime::PhysicsAxis::Z)&&
          body->IsRotationFrozen(GameSproutRuntime::PhysicsAxis::Y)&&
          !body->SetPositionFrozen(static_cast<GameSproutRuntime::PhysicsAxis>(3),true)&&
          !body->SetRotationFrozen(static_cast<GameSproutRuntime::PhysicsAxis>(255),true),
          "Per-axis constraint access or invalid-axis rejection failed");
    Check(body->SetLinearVelocity({3,0,2})&&body->SetAngularVelocity({0,2,0}),"Cannot initialize constrained velocities");
    Check(runtime.BeginExecution(error),error);Steps(runtime,30);
    const auto partial=object->GetTransform()->GetLocalPosition();
    Check(std::fabs(partial.X)<0.001f&&partial.Z>0.5f&&std::fabs(body->GetAngularVelocity().Y)<0.001f,
          "Public per-axis constraints did not reach simulation");
    Check(body->SetConstraints(GameSproutRuntime::PhysicsConstraint::FreezeAll),"Cannot freeze all six axes");
    const auto locked=object->GetTransform()->GetLocalPosition();
    Check(body->GetMotionType()==GameSproutRuntime::PhysicsMotionType::Dynamic&&body->GetConstraints()==63&&
          body->SetLinearVelocity({30,40,50})&&body->SetAngularVelocity({1,2,3})&&
          body->GetLinearVelocity().X==0&&body->GetAngularVelocity().Y==0&&
          body->AddForce({1000,2000,3000})&&body->AddImpulse({1000,2000,3000}),
          "FreezeAll changed motion semantics or retained velocity commands");
    const auto constraints=body->GetConstraints();
    Check(!body->SetConstraints(64)&&body->GetConstraints()==constraints&&body->GetMass()==3,
          "An invalid freeze edit changed previous settings");
    Check(runtime.SynchronizePhysics(error),error);
    auto* obstacle=scene->CreateGameObject("Overlapping static contact");
    obstacle->GetTransform()->SetLocalPosition({locked.X,locked.Y,locked.Z+0.9f});
    obstacle->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    unsigned staticContacts=0,dynamicContacts=0;
    const auto staticID=obstacle->GetInstanceID();
    std::uint64_t movingID=0;
    collider->RegisterCollisionEnter([&](const auto& event){
        if(event.Other.ObjectID==staticID)++staticContacts;
        if(event.Other.ObjectID==movingID)++dynamicContacts;
    });
    Steps(runtime,3);Check(staticContacts==1,"Frozen dynamic body lost collision callbacks against static geometry");
    Check(scene->DestroyGameObject(obstacle),"Cannot remove static freeze fixture");
    auto* moving=scene->CreateGameObject("Incoming dynamic body");movingID=moving->GetInstanceID();
    moving->GetTransform()->SetLocalPosition({locked.X+3,locked.Y,locked.Z});
    moving->AddComponent<GameSproutRuntime::ISphereColliderComponent>();
    auto* movingBody=moving->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    Check(movingBody->SetGravityFactor(0)&&movingBody->SetLinearDamping(0)&&movingBody->SetLinearVelocity({-3,0,0}),
          "Cannot launch incoming contact fixture");
    Steps(runtime,90);
    const auto after=object->GetTransform()->GetLocalPosition();
    Check(std::fabs(after.X-locked.X)<0.001f&&std::fabs(after.Y-locked.Y)<0.001f&&
          std::fabs(after.Z-locked.Z)<0.001f&&dynamicContacts==1&&
          moving->GetTransform()->GetLocalPosition().X>locked.X+0.9f,
          "FreezeAll allowed motion or lost blocking collision and callback participation");
    GameSproutRuntime::PhysicsRaycastHit hit;
    Check(runtime.Raycast(scene,{{locked.X,locked.Y+3,locked.Z},{0,-1,0}},10,hit,{},error)&&
          hit.Collider.ComponentID==collider->GetComponentInstanceID(),"FreezeAll collider vanished from queries");
    Check(scene->DestroyGameObject(moving)&&body->SetConstraints(GameSproutRuntime::PhysicsConstraint::None),
          "Cannot release FreezeAll");
    Steps(runtime,10);
    const auto unlocked=object->GetTransform()->GetLocalPosition();
    Check(std::fabs(unlocked.X-locked.X)<0.001f&&std::fabs(unlocked.Y-locked.Y)<0.001f&&
          std::fabs(unlocked.Z-locked.Z)<0.001f,"Unlock replayed velocity, force or impulse accumulated while frozen");
    Check(body->AddImpulse({3,0,0}),"Cannot apply a fresh impulse after unlocking");Steps(runtime,30);
    Check(object->GetTransform()->GetLocalPosition().X>unlocked.X+0.4f,
          "Unlock did not restore dynamic simulation");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),"Cannot clean public Rigidbody fixture");
}
GameSproutRuntime::IScriptComponent *ScriptWrapper = nullptr;
unsigned ScriptDestroyed = 0, ScriptReleased = 0, OwnedCalls = 0;
bool InOwnedCallback = false, SafeRelease = true, DisableInCallback = true;
class SubscriptionScript final : public GameSproutRuntime::ScriptBehaviour {
  public:
    ~SubscriptionScript() override {
        SafeRelease = SafeRelease && !InOwnedCallback;
        ++ScriptDestroyed;
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime *) override {
        auto *collider = _GameObject->GetComponent<GameSproutRuntime::IBoxColliderComponent>();
        collider->RegisterCollisionEnter([this](const auto &) {
            InOwnedCallback = true;
            ++OwnedCalls;
            if (DisableInCallback)
                ScriptWrapper->SetEnabled(false);
            SafeRelease = SafeRelease && _GameObject != nullptr;
            InOwnedCallback = false;
        });
        collider->RegisterCollisionStay([this](const auto &) {
            SafeRelease = SafeRelease && _GameObject != nullptr;
            ++OwnedCalls;
        });
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime *) override {
        SafeRelease = SafeRelease && !InOwnedCallback;
        ++ScriptReleased;
    }
};
void CheckScriptSubscriptionLifetime(GameSproutRuntime::IGameSproutRuntime &runtime) {
    GameSproutRuntime::Internal::RegisterScriptClass<SubscriptionScript>(
        "physics-subscription-fixture", "SubscriptionScript", __FILE__, {"fixture", 1}, {});
    std::string error;
    bool registered = false;
    for (const auto &type : GameSproutRuntime::Internal::ScriptModuleTypes())
        if (type.Info.TypeID == "physics-subscription-fixture")
            registered = runtime.RegisterScriptType(type, error);
    Check(registered, error);
    auto *scene = runtime.CreateScene("Owned physics callbacks");
    Ground(scene);
    auto *object = scene->CreateGameObject("Script box");
    object->GetTransform()->SetLocalPosition({0, 0.45f, 0});
    auto *collider = object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    ScriptWrapper =
        runtime.AddScriptComponent(object, "physics-subscription-fixture", {"fixture", 1}, 0, error);
    Check(ScriptWrapper, error);
    Check(runtime.BeginExecution(error), error);
    unsigned hostCalls = 0;
    collider->RegisterCollisionEnter([&](const auto &) { ++hostCalls; });
    Steps(runtime, 5);
    Check(OwnedCalls == 1 && ScriptDestroyed == 1 && ScriptReleased == 1 && SafeRelease && hostCalls == 1,
          "Script disable destroyed this inside its physics callback or retained owned callbacks");
    Steps(runtime, 5);
    Check(OwnedCalls == 1, "Disabled script callback was invoked again");
    DisableInCallback = false;
    ScriptWrapper->SetEnabled(true);
    runtime.Update(0.01f);
    Steps(runtime, 2);
    Check(OwnedCalls > 1, "Restarted script did not register a fresh live subscription");
    Check(runtime.EndExecution(error), error);
    const auto calls = OwnedCalls;
    Steps(runtime, 5);
    Check(OwnedCalls == calls && ScriptDestroyed == 2 && SafeRelease,
          "EndExecution retained script subscriptions");
    Check(runtime.BeginExecution(error), error);
    Steps(runtime, 3);
    Check(hostCalls == 2 && OwnedCalls > calls,
          "EndExecution removed unrelated host subscriptions or kept old owned ones");
    Check(runtime.EndExecution(error) && runtime.DestroyScene(scene),
          "Cannot clean script subscription fixture");
    ScriptWrapper = nullptr;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    auto *runtime = Export(argv[1]);
    try {
        Check(runtime, "Cannot load Runtime physics SDK");
        std::cout << "physics Unity world directions\n";
        CheckUnityWorldDirections(*runtime);
        std::cout << "physics queries/static pairs\n";
        CheckQueriesAndStaticPairs(*runtime);
        std::cout << "physics raycast self exclusions\n";
        CheckQueryObjectExclusions(*runtime);
        std::cout << "physics falling/callback safety\n";
        CheckFallingAndCallbackSafety(*runtime);
        std::cout << "physics triggers/removal\n";
        CheckTriggersAndRemoval(*runtime);
        std::cout << "physics compound/world isolation\n";
        CheckCompoundAndWorldIsolation(*runtime);
        std::cout << "physics body only/parents\n";
        CheckRigidbodyWithoutColliderAndParents(*runtime);
        std::cout << "physics public properties/freeze\n";
        CheckPublicRigidbodyPropertiesAndFreeze(*runtime);
        std::cout << "physics script subscriptions\n";
        CheckScriptSubscriptionLifetime(*runtime);
        delete runtime;
        std::cout << "PASS Jolt physics, compound identities/materials, queries, fixed steps and safe "
                     "Collider subscriptions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        delete runtime;
        return 1;
    }
}
