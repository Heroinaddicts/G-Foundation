#include "SceneDocument.h"
#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <stdexcept>
#include <new>
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class LifetimeProbe final:public GameSproutRuntime::IComponent {
public:
    LifetimeProbe(GameSproutRuntime::IGameObject* owner,std::uint64_t generation):IComponent(owner),Generation(generation){}
    const std::type_info& GetComponentType() const override{return typeid(LifetimeProbe);}
    const char* GetTypeName() const override{return "LifetimeProbe";}
    std::uint64_t GetComponentInstanceID() const override{return Generation;}
    std::uint64_t Generation;
};
int main() {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        runtime=Export(RUNTIME_LIBRARY_DIRECTORY);Check(runtime,"Cannot load independent Runtime");
        auto* scene=runtime->CreateScene("Physics document");auto* object=scene->CreateGameObject("Compound");
        alignas(LifetimeProbe) unsigned char slot[sizeof(LifetimeProbe)];
        SceneDocument::ObjectIDs lifetimes;
        auto* oldProbe=new(slot) LifetimeProbe(object,1);lifetimes.BindComponentFileID(oldProbe,42);
        Check(lifetimes.FindComponentFileID(oldProbe)==42,"Initial lifetime identity missing");oldProbe->~LifetimeProbe();
        auto* newProbe=new(slot) LifetimeProbe(object,2);
        Check(!lifetimes.FindComponentFileID(newProbe),"Reused component address inherited destroyed component's file ID");
        lifetimes.BindComponentFileID(newProbe,43);Check(lifetimes.FindComponentFileID(newProbe)==43,"New lifetime could not bind fresh identity");newProbe->~LifetimeProbe();
        auto* first=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* second=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* sphere=object->AddComponent<GameSproutRuntime::ISphereColliderComponent>();
        auto* capsule=object->AddComponent<GameSproutRuntime::ICapsuleColliderComponent>();
        auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
        Check(first&&second&&first!=second&&sphere&&capsule&&body,"Physics factory or repeated collider failed");
        Check(second->SetShape({{2,3,4}}),"Cannot set second collider shape");
        auto settings=second->GetColliderSettings();settings.IsTrigger=true;settings.Center={1,2,3};settings.Rotation={0.1f,0.2f,0.3f,0.9f};settings.CollisionLayer=2;settings.CollisionMask=16;
        Check(second->SetColliderSettings(settings),"Cannot set second collider surface");
        auto rigid=body->GetSettings();rigid.MotionType=GameSproutRuntime::PhysicsMotionType::Kinematic;rigid.Mass=7;
        Check(body->SetSettings(rigid),"Cannot set body");
        SceneDocument::ObjectIDs ids;Json::Value document;std::string error;
        Check(SceneDocument::Serialize(*scene,ids,document,error),error);
        Check(ids.FindComponentFileID(first)!=ids.FindComponentFileID(second),"Repeated colliders share document identity");
        const auto firstFile=ids.FindComponentFileID(first),secondFile=ids.FindComponentFileID(second);
        GameSproutRuntime::IScene* restored=nullptr;SceneDocument::ObjectIDs restoredIDs;
        Check(SceneDocument::Instantiate(document,*runtime,restored,restoredIDs,error),error);
        auto* restoredObject=restored->GetChild(0);auto colliders=restoredObject->GetComponents<GameSproutRuntime::IBoxColliderComponent>();
        Check(colliders.size()==2&&restoredIDs.FindComponentFileID(colliders[0])==firstFile&&restoredIDs.FindComponentFileID(colliders[1])==secondFile,"Roundtrip lost repeated collider identity or order");
        Check(colliders[1]->GetShape().Size.Y==3&&colliders[1]->GetColliderSettings().IsTrigger&&colliders[1]->GetColliderSettings().Center.Z==3,"Roundtrip lost physical component settings");
        Json::Value again;Check(SceneDocument::Serialize(*restored,restoredIDs,again,error)&&again==document,"Physical document roundtrip changed current data");
        for(int i=0;i<24;++i)Check(restoredObject->AddComponent<GameSproutRuntime::IBoxColliderComponent>()!=nullptr,"Cannot create repeated Collider beyond old 16-component limit");
        Check(SceneDocument::Serialize(*restored,restoredIDs,again,error),"Repeated Colliders cannot be saved beyond old 16-component limit: "+error);
        // Direct Runtime mutation can happen before SceneManager gets its event.
        // New live components must never acquire the removed Collider's file ID.
        const auto removedFile=restoredIDs.FindComponentFileID(colliders[0]);
        Check(restoredObject->RemoveComponentInstance(colliders[0]->GetComponentInstanceID()),"Cannot remove direct Runtime Collider");
        auto* replacement=restoredObject->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        Check(SceneDocument::Serialize(*restored,restoredIDs,again,error),error);
        Check(restoredIDs.FindComponentFileID(replacement)!=removedFile,"Direct Runtime remove/add reused old document identity");
        auto invalid=document;for(auto& component:invalid["Objects"][0]["Components"])if(component["Type"]=="GameSproutRuntime.CapsuleCollider")component["Shape"]["Height"]=0.01;
        Check(!SceneDocument::Validate(invalid,error),"Invalid capsule geometry accepted");
        invalid=document;for(auto& component:invalid["Objects"][0]["Components"])if(component["Type"]=="GameSproutRuntime.Rigidbody")component["Settings"]["Constraints"]=63;
        Check(!SceneDocument::Validate(invalid,error),"All-frozen unsupported body accepted");
        delete runtime;runtime=nullptr;std::cout<<"PASS repeated collider document identities and physics roundtrip\n";return 0;
    }catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}
