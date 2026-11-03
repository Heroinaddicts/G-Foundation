#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/TransformComponent.h"
#include "Components/AnimatorComponent.h"
#include "Components/ScriptComponent.h"
#include "Physics/PhysicsComponents.h"
#include "SceneDocument.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
class ProtectedCounter final:public IComponent {
public:
    explicit ProtectedCounter(IGameObject* owner):IComponent(owner){}
    const std::type_info& GetComponentType()const override{return typeid(ProtectedCounter);}
    const char* GetTypeName()const override{return "Tests.ProtectedCounter";}
    static inline unsigned Variable=0,Fixed=0,Released=0;
protected:
    void Update(IGameSproutRuntime*,float)override{++Variable;}
    void FixedUpdate(IGameSproutRuntime*,float)override{++Fixed;}
    void OnRelease(IGameSproutRuntime*)override{++Released;}
};
static_assert(Runtime::ComponentTickMask<TransformComponent>()==0);
static_assert(Runtime::ComponentTickMask<BoxColliderComponent>()==0);
static_assert(Runtime::ComponentTickMask<RigidbodyComponent>()==0);
static_assert(Runtime::ComponentTickMask<AnimatorComponent>()==1);
static_assert(Runtime::ComponentTickMask<ScriptComponent>()==3);
// Friend access to IComponent does not grant access to this derived override.
// Inaccessible hooks must remain scheduled, rather than silently disappearing.
static_assert(Runtime::ComponentTickMask<ProtectedCounter>()==3);

Json::Value Node(std::uint64_t id,std::uint64_t parent,const char* name){
    Json::Value value(Json::objectValue);value["ID"]=Json::UInt64(id);value["ParentID"]=Json::UInt64(parent);
    value["Name"]=name;value["Active"]=true;value["Components"]=Json::Value(Json::arrayValue);return value;
}
Json::Value Document(){
    Json::Value value(Json::objectValue);value["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion);value["Name"]="Assembly";
    value["MainCameraID"]=Json::UInt64(0);value["Objects"]=Json::Value(Json::arrayValue);return value;
}
void Scheduling(){
    Runtime runtime;std::string error;Check(runtime.BeginConstruction(error),error.c_str());
    auto* scene=runtime.CreateScene("Schedule");
    {Runtime foreign;auto* other=foreign.CreateScene("Foreign");
        Check(!runtime.IsInScene(&dynamic_cast<ObjectAccess*>(other)->State()),"Scene membership accepted another Runtime's root");}
    for(unsigned index=0;index<5000;++index){auto* object=scene->CreateGameObject("Static");
        object->AddComponent<IMeshComponent>();object->AddComponent<IMeshRendererComponent>();object->AddComponent<IBoxColliderComponent>();
        auto& state=dynamic_cast<ObjectAccess*>(object)->State();
        for(const auto& entry:state.Components)Check(entry.TickMask==0,"Inherited empty builtin hook was scheduled");
    }
    Check(runtime.EndConstruction(error),error.c_str());
    auto* owner=scene->CreateGameObject("Protected hooks");
    auto& state=dynamic_cast<ObjectAccess*>(owner)->State();
    const auto serial=++state.NextComponentSerial;
    auto* counter=new ProtectedCounter(owner);
    GameObjectState::ComponentEntry entry{counter,serial};entry.Initialized=true;
    state.Components.push_back(entry); // Unknown implementation defaults to both hooks.
    runtime.RegisterComponentTick(state.ID,entry.TickMask);
    runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(ProtectedCounter::Variable==1&&ProtectedCounter::Fixed==1,"Protected custom hooks stopped running");
    auto* parent=scene->CreateGameObject("Parent");Check(owner->SetParent(parent,false),"Cannot parent scheduled object");
    parent->SetActive(false);runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(ProtectedCounter::Variable==1&&ProtectedCounter::Fixed==1,"Inactive parent did not suspend hooks");
    parent->SetActive(true);runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(ProtectedCounter::Variable==2&&ProtectedCounter::Fixed==2,"Activation did not restore scheduled hooks");
    Check(owner->SetParent(nullptr,false),"Cannot detach scheduled object");runtime.Update(.01f);
    Check(ProtectedCounter::Variable==2&&!owner->IsActiveInHierarchy(),"Detached object continued ticking");
    Check(owner->SetParent(parent,false),"Cannot reattach scheduled object");runtime.Update(.01f);
    Check(ProtectedCounter::Variable==3,"Reattached object lost its scheduling registration");
    Check(owner->RemoveComponentInstance(serial),"Cannot remove custom hook");runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(ProtectedCounter::Variable==3&&ProtectedCounter::Fixed==2&&ProtectedCounter::Released==1,"Removed hook retained a live scheduling entry");
    const auto start=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<1000;++frame){runtime.Update(.01f);runtime.FixedUpdate(.02f);}
    std::cout<<"Static 5000-object 1000 variable/fixed pairs ms="
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
    Check(runtime.DestroyScene(scene),"Cannot release scheduled/static scene");
}
void Assembly(){
    Runtime runtime;std::string error;SceneDocument::ObjectIDs ids;
    Json::Value document=Document();
    document["Objects"].append(Node(10,100,"P first"));
    document["Objects"].append(Node(20,100,"P second"));
    document["Objects"].append(Node(200,0,"Z root first"));
    document["Objects"].append(Node(30,200,"Z child"));
    document["Objects"].append(Node(100,0,"P root second"));
    Json::Value transform(Json::objectValue);transform["ID"]=Json::UInt64(1000);transform["Type"]="GameSproutRuntime.Transform";
    document["Objects"][0]["Components"].append(transform);
    const Json::Value original=document;
    IScene* scene=nullptr;
    Check(SceneDocument::Instantiate(document,runtime,scene,ids,error),error.c_str());
    Check(document==original,"Assembly mutated the borrowed component JSON");
    auto malformed=document;
    malformed["Objects"][0]["Components"][0]["ID"]=true;
    Check(!SceneDocument::Validate(malformed,error),"Boolean component ID passed central identity validation");
    malformed=document;malformed["Objects"][0]["Components"][0]["Unknown"]=true;
    Check(!SceneDocument::Validate(malformed,error),"Borrowed component JSON bypassed unknown-field validation");
    malformed=document;
    malformed["Objects"][0]["Components"][0]["Type"]="GameSproutRuntime.BoxCollider";
    malformed["Objects"][0]["Components"][0]["Settings"]["ID"]=Json::UInt64(1001);
    Check(!SceneDocument::Validate(malformed,error)&&error.find("Settings.ID")!=std::string::npos,
        "Top-level metadata allowance leaked into nested physics settings");
    malformed=document;
    malformed["Objects"][0]["Components"][0]["Type"]="GameSproutRuntime.Camera";
    malformed["Objects"][0]["Components"][0]["Settings"]["ID"]=Json::UInt64(1001);
    Check(!SceneDocument::Validate(malformed,error)&&error.find("Settings.ID")!=std::string::npos,
        "Top-level metadata allowance leaked into nested builtin settings");
    Check(scene->GetChildCount()==2&&scene->GetChild(0)->GetName()=="Z root first"&&
        scene->GetChild(1)->GetName()=="P root second","Topological assembly changed root sibling order");
    auto* p=scene->GetChild(1);Check(p->GetChildCount()==2&&p->GetChild(0)->GetName()=="P first"&&
        p->GetChild(1)->GetName()=="P second","Child-before-parent assembly changed sibling order");
    Check(runtime.QuerySceneObjectByFileID(scene,10)==p->GetChild(0),"Assembly changed stable object identity");
    Json::Value saved;Check(SceneDocument::Serialize(*scene,ids,saved,error),error.c_str());
    IScene* clone=nullptr;SceneDocument::ObjectIDs cloneIDs;
    Check(SceneDocument::Instantiate(saved,runtime,clone,cloneIDs,error),error.c_str());
    Check(clone->GetChild(1)->GetChild(1)->GetName()=="P second","Clone changed hierarchy");
    Check(runtime.DestroyScene(scene)&&runtime.DestroyScene(clone),"Cannot release hierarchy fixture");
    document=Document();
    // Maximum file depth is 1024. A reversed chain exercises parents appearing
    // much later in the file without recursive allocation/reparenting.
    for(unsigned index=512;index>0;--index)document["Objects"].append(Node(index,index==1?0:index-1,"Deep"));
    Check(SceneDocument::Instantiate(document,runtime,scene,ids,error),error.c_str());
    auto* node=scene->GetChild(0);unsigned depth=1;
    while(node->GetChildCount()){node=node->GetChild(0);++depth;}
    Check(depth==512&&node->IsActiveInHierarchy(),"Reversed deep hierarchy was not assembled/activated correctly");
    Check(runtime.DestroyScene(scene),"Cannot release deep fixture");
    document=Document();for(unsigned index=1;index<=6000;++index)document["Objects"].append(Node(index,0,"Wide"));
    const auto start=std::chrono::steady_clock::now();
    Check(SceneDocument::Instantiate(document,runtime,scene,ids,error),error.c_str());
    Check(scene->GetChildCount()==6000,"Wide assembly lost objects");
    const auto created=std::chrono::steady_clock::now();
    Check(runtime.DestroyScene(scene),"Cannot release wide fixture");
    std::cout<<"Wide 6000-object instantiate ms="<<std::chrono::duration<double,std::milli>(created-start).count()
        <<" destroy ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-created).count()<<'\n';
}
}
int main(){try{Scheduling();Assembly();std::cout<<"PASS tick policy, protected hooks, activation, detach, removal, parent-first assembly, stable sibling order and deep/wide destruction\n";return 0;}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
