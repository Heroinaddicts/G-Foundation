#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
std::map<std::uint64_t,unsigned> NativeReads;
unsigned Updates=0,FixedUpdates=0;
GameSproutRuntime::IGameSproutRuntime* ReaderRuntime=nullptr;
GameSproutRuntime::IRuntimePropertyObserver* RemoveDuringRead=nullptr;
}
class LiveScript final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit LiveScript(GameSproutRuntime::IGameSproutRuntime*) {}
    float Number=1;
    GameSproutRuntime::IGameObject* Target=nullptr;
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Number;++Updates;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++FixedUpdates;}
};
namespace {
class Observer final : public GameSproutRuntime::IRuntimePropertyObserver {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    std::uint64_t Object=0;unsigned Calls=0;bool Unsubscribe=false,Safe=false;
    void OnRuntimeObjectPropertiesChanged(std::uint64_t scene,std::uint64_t object) override {
        Check(scene&&object==Object,"Observer delivered the wrong target");++Calls;
        std::string error;Safe=Runtime->IsExecuting()&&!Runtime->SynchronizePhysics(error);
        if(Unsubscribe)Runtime->UnregisterObjectPropertyObserver(Object,this);
    }
};
void CheckLiveFields(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto number=GameSproutRuntime::Internal::MakeField<LiveScript>("number","Number",&LiveScript::Number,1.f);
    const auto read=number.Read;
    number.Read=[read](const GameSproutRuntime::IScript& script,GameSproutRuntime::ScriptValue& value,const GameSproutRuntime::ScriptReferenceContext& context,std::string& error) {
        if(RemoveDuringRead) {
            auto* observer=RemoveDuringRead;RemoveDuringRead=nullptr;
            ReaderRuntime->UnregisterObjectPropertyObserver(script.GetGameObject()->GetInstanceID(),observer);
        }
        ++NativeReads[script.GetGameObject()->GetInstanceID()];return read(script,value,context,error);
    };
    auto reference=GameSproutRuntime::Internal::MakeField<LiveScript>("target","Target",&LiveScript::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr));
    GameSproutRuntime::Internal::RegisterScriptClass<LiveScript>("live-script","LiveScript","Live.hpp",
        {"0123456789abcdef0123456789abcdef",1},{number,reference});
    std::string error;Check(runtime.RegisterScriptType(GameSproutRuntime::Internal::ScriptModuleTypes().back(),error),error);
    auto* scene=runtime.CreateScene("Live inspector");auto* watched=scene->CreateGameObject("Observed");
    auto* ignored=scene->CreateGameObject("Unobserved");
    auto* first=runtime.AddScriptComponent(watched,"live-script",{"0123456789abcdef0123456789abcdef",1},0,error);
    auto* second=runtime.AddScriptComponent(ignored,"live-script",{"0123456789abcdef0123456789abcdef",1},0,error);Check(first&&second,error);
    std::map<std::string,GameSproutRuntime::ScriptValue> fields;
    Check(first->ReadFields(fields,error)&&fields.at("number").Number==1,"Stopped reader did not use authored defaults");
    Observer observer;observer.Runtime=&runtime;observer.Object=watched->GetInstanceID();
    Observer removed;removed.Runtime=&runtime;removed.Object=watched->GetInstanceID();removed.Unsubscribe=true;
    Check(runtime.RegisterObjectPropertyObserver(observer.Object,&observer)&&
          runtime.RegisterObjectPropertyObserver(removed.Object,&removed)&&
          !runtime.RegisterObjectPropertyObserver(observer.Object,&observer),"Object subscription identity is not unique per observer pair");
    Check(runtime.BeginExecution(error),error);runtime.ProcessPendingEvents();
    observer.Calls=0;NativeReads.clear();
    runtime.Update(.01f);
    Check(observer.Calls==1&&observer.Safe&&removed.Calls==1,"Native script change delivery or self-unregistration is unsafe");
    Check(NativeReads[observer.Object]>0&&NativeReads[ignored->GetInstanceID()]==0,"Native readers scanned an unobserved object");
    Check(first->ReadFields(fields,error)&&fields.at("number").Number==2,"Live reader returned the authored baseline");
    Check(first->GetFields().at("number").Number==1,"Live read mutated the authored baseline");
    auto value=fields.at("number");value.Number=1;
    Check(first->SetField("number",value,error)&&first->ReadFields(fields,error)&&fields.at("number").Number==1,
          "Writing the authored number failed to replace a different live value");
    runtime.ProcessPendingEvents();
    const auto pausedReads=NativeReads;const auto oldUpdates=Updates,oldFixed=FixedUpdates;
    for(unsigned index=0;index<20;++index)runtime.ProcessPendingEvents();
    Check(NativeReads==pausedReads&&Updates==oldUpdates&&FixedUpdates==oldFixed,"Idle pending-event delivery polled native fields or advanced scripts");
    ReaderRuntime=&runtime;RemoveDuringRead=&observer;const auto beforeReaderRemoval=observer.Calls;
    runtime.Update(.01f);
    Check(!RemoveDuringRead&&observer.Calls==beforeReaderRemoval&&runtime.RegisterObjectPropertyObserver(observer.Object,&observer),
          "Native field reader unregistration retained or invalidated an observer entry");
    auto* created=runtime.CreateGameObject("Spawned",watched);std::uint64_t fileID=0;
    Check(created&&!runtime.GetSceneObjectFileID(created,fileID),"Spawned fixture unexpectedly has a document identity");
    value={};value.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    value.SceneObject={true,0,scene->GetInstanceID(),created->GetInstanceID()};
    Check(first->SetField("target",value,error)&&first->ReadFields(fields,error)&&fields.at("target")==value,
          "Session-only object reference did not resolve to the spawned object");
    auto malformed=value;malformed.SceneObject.ObjectFileID=5;
    Check(!first->SetField("target",malformed,error),"Mixed persistent/runtime reference was accepted");
    auto* another=runtime.CreateScene("Other world");auto* outsider=another->CreateGameObject("Outsider");
    malformed=value;malformed.SceneObject.RuntimeSceneID=another->GetInstanceID();malformed.SceneObject.RuntimeObjectID=outsider->GetInstanceID();
    Check(!first->SetField("target",malformed,error),"Cross-scene runtime object reference was accepted");
    Check(scene->DestroyGameObject(created)&&first->ReadFields(fields,error)&&!fields.at("target").SceneObject.Assigned,
          "Destroying a session-only object did not null the actual native field");
    Check(!first->SetField("target",value,error),"Stale runtime object reference was accepted");
    Check(runtime.EndExecution(error),error);
    Check(!first->SetField("target",value,error)&&!first->RestoreSerializedFields({{"target",value}},error),
          "Runtime reference entered stopped/authored restoration");
    runtime.UnregisterObjectPropertyObserver(observer.Object,&observer);
    Check(runtime.DestroyScene(scene)&&runtime.DestroyScene(another),"Cannot destroy live field fixtures");
}
void CheckPausedPhysics(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;auto* scene=runtime.CreateScene("Paused physics edit");
    auto* object=scene->CreateGameObject("Body");object->GetTransform()->SetLocalPosition({0,4,0});
    auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    auto* collider=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();Check(body&&collider,"Physics component creation failed");
    unsigned collisions=0;collider->RegisterCollisionEnter([&](const auto&){++collisions;});
    Check(runtime.BeginExecution(error),error);runtime.FixedUpdate(1.f/60.f);
    Observer observer;observer.Runtime=&runtime;observer.Object=object->GetInstanceID();
    Check(runtime.RegisterObjectPropertyObserver(observer.Object,&observer),"Cannot observe a moving physics body");
    runtime.FixedUpdate(1.f/60.f);Check(observer.Calls==1,"Simulated Transform did not notify its selected observer");
    const auto oldVelocity=body->GetLinearVelocity();const auto oldUpdates=Updates,oldFixed=FixedUpdates;
    object->GetTransform()->SetLocalPosition({7,10,0});auto shape=collider->GetShape();shape.Size={2,2,2};Check(collider->SetShape(shape),"Cannot resize paused Collider");
    Check(runtime.SynchronizePhysics(error),error);
    Check(object->GetTransform()->GetLocalPosition().X==7&&object->GetTransform()->GetLocalPosition().Y==10,
          "Zero-time synchronization integrated or overwrote the edited Transform");
    Check(std::fabs(body->GetLinearVelocity().Y-oldVelocity.Y)<1e-6f&&collisions==0&&Updates==oldUpdates&&FixedUpdates==oldFixed,
          "Zero-time synchronization advanced scripts/velocity or dispatched contacts");
    const auto calls=observer.Calls;Check(runtime.SynchronizePhysics(error)&&observer.Calls==calls,"Unchanged physics synchronization emitted repeated property events");
    runtime.UnregisterObjectPropertyObserver(observer.Object,&observer);
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),error);
}
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        CheckLiveFields(*runtime);CheckPausedPhysics(*runtime);
        delete runtime;std::cout<<"PASS live native fields, session object handles, selective observers and zero-time physics synchronization\n";return 0;
    }catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}
