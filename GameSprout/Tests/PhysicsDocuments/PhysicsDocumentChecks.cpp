#include "SceneDocument.h"
#include "PrefabDocument.h"
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
        auto* mesh=object->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
        auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
        Check(first&&second&&first!=second&&sphere&&capsule&&mesh&&body,"Physics factory or repeated collider failed");
        const GameSproutRuntime::ResourceReference meshReference{"abcdefabcdefabcdefabcdefabcdefab",42};
        Check(mesh->SetShape({meshReference,true}),"Cannot set mesh collision source");
        auto meshSettings=mesh->GetColliderSettings();meshSettings.IsTrigger=true;Check(mesh->SetColliderSettings(meshSettings),"Cannot set convex mesh trigger");
        Check(second->SetShape({{2,3,4}}),"Cannot set second collider shape");
        auto settings=second->GetColliderSettings();settings.IsTrigger=true;settings.Center={1,2,3};settings.Rotation={0.1f,0.2f,0.3f,0.9f};second->GetGameObject()->SetLayer(2);
        Check(second->SetColliderSettings(settings),"Cannot set second collider surface");
        auto rigid=body->GetSettings();rigid.MotionType=GameSproutRuntime::PhysicsMotionType::Kinematic;rigid.Mass=7;rigid.Constraints=63;
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
        Check(restoredObject->GetComponent<GameSproutRuntime::IRigidbodyComponent>()->GetConstraints()==63,
              "Roundtrip lost FreezeAll constraints");
        auto* restoredMesh=restoredObject->GetComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(restoredMesh&&restoredMesh->GetShape().Mesh==meshReference&&restoredMesh->GetShape().Convex
            &&restoredMesh->GetColliderSettings().IsTrigger,"Scene roundtrip lost mesh collision reference/convex/trigger");
        Check(restoredIDs.FindComponentFileID(restoredMesh)==ids.FindComponentFileID(mesh),"Scene roundtrip changed MeshCollider stable component identity");
        Json::Value again;Check(SceneDocument::Serialize(*restored,restoredIDs,again,error)&&again==document,"Physical document roundtrip changed current data");
        auto prefabSource=document;prefabSource["RootComponents"]=Json::Value(Json::arrayValue);prefabSource["RootMainCamera"]=false;
        std::vector<std::uint8_t> prefabBytes;Json::Value prefabDocument;
        Check(PrefabDocument::Encode(prefabSource,prefabBytes,error)&&PrefabDocument::Decode(prefabBytes,prefabDocument,error),error);
        Check(prefabDocument==prefabSource,"Prefab encoding changed missing mesh resource identity");
        GameSproutRuntime::IGameObject* instance=nullptr;std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        Check(SceneDocument::InstantiatePrefab(prefabDocument,*runtime,*scene,*scene,instance,mappings,error),error);
        auto* prefabMesh=instance->GetComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(prefabMesh&&prefabMesh->GetShape().Mesh==meshReference&&prefabMesh->GetShape().Convex
            &&prefabMesh->GetColliderSettings().IsTrigger,"Prefab instantiation lost MeshCollider settings");
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
        invalid=document;for(auto& component:invalid["Objects"][0]["Components"])if(component["Type"]=="GameSproutRuntime.Rigidbody")component["Settings"]["Constraints"]=64;
        Check(!SceneDocument::Validate(invalid,error),"Unknown Rigidbody constraint bit accepted");
        invalid=document;for(auto& component:invalid["Objects"][0]["Components"])
            if(component["Type"]=="GameSproutRuntime.BoxCollider")component["Settings"]["CollisionMask"]=16;
        Check(!SceneDocument::Validate(invalid,error),"Removed per-Collider collision mask accepted");
        invalid=document;for(auto& component:invalid["Objects"][0]["Components"])
            if(component["Type"]=="GameSproutRuntime.MeshCollider")component["Shape"]["Mesh"]["LocalID"]=Json::UInt64(0);
        Check(!SceneDocument::Validate(invalid,error),"Invalid MeshCollider resource identity accepted");
        invalid=document;for(auto& component:invalid["Objects"][0]["Components"])
            if(component["Type"]=="GameSproutRuntime.MeshCollider")component["Shape"]["Convex"]=false;
        Check(!SceneDocument::Validate(invalid,error),"Nonconvex MeshCollider trigger accepted");
        delete runtime;runtime=nullptr;std::cout<<"PASS repeated collider document identities and physics roundtrip\n";return 0;
    }catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}
