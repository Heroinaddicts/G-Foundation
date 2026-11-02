#include "GameSproutRuntimeExport.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Events final : public GameSproutRuntime::IRuntimeListener {
public:
    std::uint64_t ObjectID=0;unsigned Changes=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.ObjectID==ObjectID&&event.Type==GameSproutRuntime::SceneNodeEventType::StaticFlagsChanged)++Changes;
    }
};
void CheckStorageAndEvents(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=runtime.CreateScene("Static declarations");
    auto* parent=scene->CreateGameObject("Parent");auto* child=scene->CreateGameObject("Child",parent);
    Check(scene->GetStaticFlags()==GameSproutRuntime::StaticFlags::None&&!parent->IsStatic()&&!child->IsStatic(),"Static declarations must default to None");
    runtime.ProcessPendingEvents();Events events;events.ObjectID=child->GetInstanceID();
    Check(runtime.RegisterListener(&events),"Cannot subscribe to StaticFlags events");
    const auto selected=GameSproutRuntime::StaticFlags::ContributeGI|GameSproutRuntime::StaticFlags::BatchingStatic|
        GameSproutRuntime::StaticFlags::ReflectionProbeStatic;
    Check(child->SetStaticFlags(selected)&&child->GetStaticFlags()==selected&&child->IsStatic(),"Cannot preserve independently selected StaticFlags");
    Check(child->SetStaticFlags(selected),"Idempotent assignment failed");
    const auto invalid=static_cast<GameSproutRuntime::StaticFlags>(0x80u|static_cast<std::uint32_t>(selected));
    Check(!child->SetStaticFlags(invalid)&&child->GetStaticFlags()==selected,"Unknown static bits changed the declaration");
    runtime.ProcessPendingEvents();Check(events.Changes==1,"Static change event was missing or repeated for idempotent/invalid writes");
    Check(parent->SetStaticFlags(GameSproutRuntime::StaticFlags::Everything)&&child->GetStaticFlags()==selected,"Static flags inherited automatically to child");
    Check(child->SetStaticFlags(GameSproutRuntime::StaticFlags::None)&&!child->IsStatic()&&parent->IsStatic(),"Clearing a child modified parent flags");
    Check(child->SetStaticFlags(GameSproutRuntime::StaticFlags::Everything),"Cannot restore authored static flags");
    child->GetTransform()->SetLocalPosition({2,4,6});
    Check(child->GetTransform()->GetLocalPosition().Y==4,"Static declarations unexpectedly froze Transform");
    runtime.ProcessPendingEvents();Check(events.Changes==3,"Clear/restore static declaration events were lost");
    const auto childID=child->GetInstanceID();bool rejected=false;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject* object){
        if(object!=parent)return;
        Check(scene->DestroyGameObject(child),"Cannot queue fixture destruction");
        rejected=!child->SetStaticFlags(GameSproutRuntime::StaticFlags::None);
    });
    Check(rejected&&!runtime.QueryGameObject(childID),"Pending-destroy object accepted a static declaration change");
    runtime.UnregisterListener(&events);Check(runtime.DestroyScene(scene),"Cannot destroy static declaration fixture");
}
void CheckPhysicsIndependence(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=runtime.CreateScene("Static declaration is not motion type");
    auto* object=scene->CreateGameObject("Dynamic object");
    object->GetTransform()->SetLocalPosition({0,5,0});
    auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto* collider=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(body&&collider&&object->SetStaticFlags(GameSproutRuntime::StaticFlags::Everything),"Cannot construct marked dynamic fixture");
    Check(body->GetSettings().MotionType==GameSproutRuntime::PhysicsMotionType::Dynamic&&collider->GetAttachedRigidbody()==body,
        "Static declarations changed Rigidbody ownership or motion type");
    auto* floor=scene->CreateGameObject("Unmarked static collider");
    floor->GetTransform()->SetLocalPosition({0,-5,0});auto* staticCollider=floor->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    Check(staticCollider&&!floor->IsStatic()&&!staticCollider->GetAttachedRigidbody(),"Unmarked collider unexpectedly needs static declarations to be physical static geometry");
    std::string error;Check(runtime.BeginExecution(error),error);
    for(unsigned step=0;step<6;++step)runtime.FixedUpdate(1.f/60.f);
    Check(object->GetTransform()->GetLocalPosition().Y<4.99f&&body->GetLinearVelocity().Y<0,
        "A static authoring declaration froze dynamic physics");
    Check(body->GetSettings().MotionType==GameSproutRuntime::PhysicsMotionType::Dynamic,
        "Physics rewrote Dynamic motion from authoring metadata");
    Check(runtime.EndExecution(error),error);Check(runtime.DestroyScene(scene),"Cannot destroy static/physics fixture");
}
}
int main(int argc,char** argv) {
    static_assert(sizeof(GameSproutRuntime::StaticFlags)==sizeof(std::uint32_t));
    static_assert(GameSproutRuntime::IsValidStaticFlags(GameSproutRuntime::StaticFlags::Everything));
    static_assert(!GameSproutRuntime::IsValidStaticFlags(static_cast<GameSproutRuntime::StaticFlags>(128)));
    static_assert(~GameSproutRuntime::StaticFlags::Everything==GameSproutRuntime::StaticFlags::None);
    static_assert(~static_cast<GameSproutRuntime::StaticFlags>(128)==GameSproutRuntime::StaticFlags::Everything);
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass the Runtime library directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        CheckStorageAndEvents(*runtime);CheckPhysicsIndependence(*runtime);
        delete runtime;std::cout<<"PASS StaticFlags declarations, events, validation and physics independence\n";return 0;
    }catch(const std::exception& exception){delete runtime;std::cerr<<exception.what()<<'\n';return 1;}
}
