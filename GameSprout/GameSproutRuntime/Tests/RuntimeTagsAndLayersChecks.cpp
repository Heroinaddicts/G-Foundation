#include "GameSproutRuntimeExport.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
class Events final:public GameSproutRuntime::IRuntimeListener {
public:
    unsigned Tags=0,Layers=0;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        Tags+=event.Type==GameSproutRuntime::SceneNodeEventType::TagChanged;
        Layers+=event.Type==GameSproutRuntime::SceneNodeEventType::LayerChanged;
    }
};
void CheckDefinitions(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto settings=runtime.GetProjectSettings();
    Check(settings.LayerNames[0]=="Default"&&settings.LayerNames[1]=="Ignore Raycast"&&settings.LayerNames[2]=="UI"&&settings.LayerNames[3].empty(),"Built-in layer indices mismatch");
    Check(settings.Tags.size()==1&&settings.Tags[0].ID==0&&settings.Tags[0].Name=="Untagged","Unexpected magic default tags");
    settings.LayerNames[3]="Characters";settings.LayerNames[31]="High bit";
    settings.Tags.push_back({1,"Enemy"});settings.Tags.push_back({2,"Door"});settings.NextTagID=3;
    Check(runtime.SetProjectSettings(settings,error),error);
    auto invalid=settings;invalid.LayerNames[0]="Other";
    Check(!runtime.SetProjectSettings(invalid,error)&&runtime.GetProjectSettings()==settings,"Built-in rename was not rejected atomically");
    invalid=settings;invalid.CollisionMatrix[3]&=~(1u<<4);
    Check(!runtime.SetProjectSettings(invalid,error)&&runtime.GetProjectSettings()==settings,"Asymmetric matrix was accepted");
    invalid=settings;invalid.Tags.push_back({3,"Enemy"});invalid.NextTagID=4;
    Check(!runtime.SetProjectSettings(invalid,error),"Duplicate tag name was accepted");
    invalid=settings;invalid.LayerNames[4]="Characters";
    Check(!runtime.SetProjectSettings(invalid,error),"Duplicate layer name was accepted");
    invalid=settings;invalid.Tags[1].Name=std::string("Bad\0Tag",7);
    Check(!runtime.SetProjectSettings(invalid,error),"Embedded NUL tag was accepted");
}
void CheckObjects(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto* scene=runtime.CreateScene("Tags");auto* other=runtime.CreateScene("Other scene");
    auto* parent=scene->CreateGameObject("Parent");auto* first=scene->CreateGameObject("First",parent);
    auto* second=other->CreateGameObject("Second");auto* detached=runtime.CreateGameObject("Unattached");
    Check(first->SetTag("Enemy")&&second->SetTagID(1)&&detached->SetTagID(1),"Cannot assign defined tag");
    Check(first->GetTagID()==1&&first->CompareTag("Enemy")&&!first->CompareTag("Undefined"),"Tag lookup mismatch");
    Check(!first->SetTag("Undefined")&&!first->SetTagID(999)&&first->GetTagID()==1,"Undefined tag changed the object");
    Check(parent->GetTagID()==0&&first->GetLayer()==0,"Tag assignment propagated to parent");
    Check(runtime.FindGameObjectWithTag("Enemy")==first,"Single tag query has wrong stable order");
    Check(runtime.FindGameObjectsWithTag("Enemy").size()==2&&runtime.FindGameObjectsWithTag("Enemy",scene).size()==1,
          "Tag query included detached nodes or ignored scene scope");
    parent->SetActive(false);
    Check(runtime.FindGameObjectWithTag("Enemy")==second,"Tag index query included an inactive subtree");
    Check(runtime.FindGameObjectsWithTag("Enemy",scene).empty(),"Scene tag query included inactive children");
    parent->SetActive(true);
    Check(parent->SetLayer(3)&&first->GetLayer()==0,"Layer incorrectly inherited to children");
    Check(first->SetLayer(31)&&first->GetLayer()==31&&!first->SetLayer(32)&&first->GetLayer()==31,"Layer bounds are not preserved");
    runtime.ProcessPendingEvents();Events events;Check(runtime.RegisterListener(&events),"Cannot subscribe");
    Check(first->SetTagID(2)&&first->SetLayer(3),"Cannot update object classification");
    Check(first->SetTagID(2)&&first->SetLayer(3),"Idempotent classification failed");
    runtime.ProcessPendingEvents();Check(events.Tags==1&&events.Layers==1,"Classification events were missing or duplicated");
    auto settings=runtime.GetProjectSettings();settings.Tags[2].Name="Gate";
    Check(runtime.SetProjectSettings(settings,error)&&first->GetTagID()==2&&first->GetTag()=="Gate",error);
    Check(!runtime.FindGameObjectWithTag("Door")&&runtime.FindGameObjectWithTag("Gate")==first,"Rename did not preserve indexed Tag identity");
    runtime.ProcessPendingEvents();Check(events.Tags==2,"Tag rename did not notify existing objects");
    first->SetActive(false);auto removal=settings;removal.Tags.erase(removal.Tags.begin()+2);
    Check(!runtime.CanApplyProjectSettings(removal,error)&&!runtime.SetProjectSettings(removal,error),"In-use inactive Tag was removed");
    Check(first->SetTagID(0)&&runtime.SetProjectSettings(removal,error),error);
    auto reused=removal;reused.Tags.push_back({2,"Reused"});
    Check(!runtime.SetProjectSettings(reused,error),"Deleted Tag identity was reused");
    removal.Tags.erase(removal.Tags.begin()+1);
    Check(!runtime.SetProjectSettings(removal,error),"Detached/live object Tag usage was ignored");
    Check(scene->DestroyGameObject(parent)&&runtime.DestroyScene(scene)&&runtime.DestroyScene(other),"Cannot destroy tag fixtures");
    Check(detached->SetTagID(0),"Cannot clear detached Tag");Check(runtime.EndFrame(error),error);
    Check(runtime.SetProjectSettings(removal,error),error);
    Check(runtime.FindGameObjectsWithTag("Enemy").empty()&&runtime.FindGameObjectsWithTag("Untagged").empty(),"Destroyed objects leaked into Tag index");
    runtime.UnregisterListener(&events);
}
void CheckPhysics(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;auto settings=runtime.GetProjectSettings();
    settings.CollisionMatrix[3]&=~(1u<<4);settings.CollisionMatrix[4]&=~(1u<<3);
    Check(runtime.SetProjectSettings(settings,error),error);
    auto* scene=runtime.CreateScene("Matrix");auto* target=scene->CreateGameObject("Static");
    Check(target->SetLayer(3),"Cannot set target layer");target->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto* parent=scene->CreateGameObject("Rigidbody parent");auto* body=parent->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto bodySettings=body->GetSettings();bodySettings.MotionType=GameSproutRuntime::PhysicsMotionType::Kinematic;
    Check(body->SetSettings(bodySettings),"Cannot configure kinematic body");
    auto* child=scene->CreateGameObject("Trigger",parent);Check(child->SetLayer(4),"Cannot set child collider layer");
    auto* trigger=child->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto colliderSettings=trigger->GetColliderSettings();colliderSettings.IsTrigger=true;
    Check(trigger->SetColliderSettings(colliderSettings),"Cannot configure Trigger");
    unsigned entered=0;trigger->RegisterTriggerEnter([&](const auto&){++entered;});
    Check(runtime.BeginExecution(error),error);
    for(unsigned i=0;i<3;++i)runtime.FixedUpdate(1.f/60.f);
    Check(entered==0,"Matrix ignored the child Collider Layer and used its Rigidbody parent's Layer");
    GameSproutRuntime::PhysicsRaycastHit hit;GameSproutRuntime::PhysicsQuerySettings query;query.CollisionMask=1u<<3;
    Check(runtime.Raycast(scene,{{0,3,0},{0,-1,0}},10,hit,query,error)&&hit.Collider.ObjectID==target->GetInstanceID(),
          "Raycast was incorrectly filtered by Layer Collision Matrix");
    settings.CollisionMatrix[3]|=1u<<4;settings.CollisionMatrix[4]|=1u<<3;
    Check(runtime.SetProjectSettings(settings,error),error);
    for(unsigned i=0;i<3;++i)runtime.FixedUpdate(1.f/60.f);
    Check(entered==1,"Changing collision matrix did not refilter existing bodies");
    Check(runtime.EndExecution(error),error);
    Check(target->SetLayer(GameSproutRuntime::IgnoreRaycastLayer),"Cannot set Ignore Raycast");
    query={};
    Check(!runtime.Raycast(scene,{{0,3,0},{0,-1,0}},10,hit,query,error),"Default ray query included Ignore Raycast layer");
    query.CollisionMask=1u<<GameSproutRuntime::IgnoreRaycastLayer;
    Check(runtime.Raycast(scene,{{0,3,0},{0,-1,0}},10,hit,query,error)&&hit.Collider.ObjectID==target->GetInstanceID(),
          "Explicit query mask could not include Ignore Raycast");
    Check(runtime.DestroyScene(scene),"Cannot destroy physics fixture");
}
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime library directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        CheckDefinitions(*runtime);CheckObjects(*runtime);CheckPhysics(*runtime);
        delete runtime;std::cout<<"Runtime Tag, Layer, collision matrix and query checks passed\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<error.what()<<'\n';return 1;}
}
