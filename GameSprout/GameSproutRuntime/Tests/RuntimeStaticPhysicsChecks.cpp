#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {
void Check(bool condition,const std::string& error){if(!condition)throw std::runtime_error(error);}
class Provider final:public IRuntimeResourceProvider {
public:
    std::uint64_t Revision=1;
    unsigned MaterialLoads=0;
    bool Valid=true;
    std::uint64_t GetRevision()const override{return Revision;}
    bool LoadMesh(const ResourceReference&,MeshRenderData&,std::string&)override{return false;}
    bool LoadTexture(const ResourceReference&,TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const ResourceReference&,MaterialRenderData&,std::string&)override{return false;}
    bool LoadPhysicsMaterial(const ResourceReference&,PhysicsMaterialData& value,std::string& error)override{
        ++MaterialLoads;if(!Valid){error="Static physics material changed";return false;}
        value={};return true;
    }
};
}
int main(int argc,char** argv){
    IGameSproutRuntime* runtime=nullptr;Provider provider;std::string error;
    try{
        Check(argc==2,"Pass Runtime library directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        runtime->SetResourceProvider(&provider);auto* scene=runtime->CreateScene("Static synchronization");
        Check(runtime->BeginConstruction(error),error);
        auto* parent=scene->CreateGameObject("Parent");auto* ground=scene->CreateGameObject("Ground",parent);
        auto* collider=ground->AddComponent<IBoxColliderComponent>();
        ColliderSettings settings=collider->GetColliderSettings();
        settings.Material={"0123456789abcdef0123456789abcdef",1};Check(collider->SetColliderSettings(settings),"Material binding failed");
        for(unsigned index=0;index<1000;++index){
            auto* node=scene->CreateGameObject("Static box");
            node->GetTransform()->SetLocalPosition({float(index%100)*3,0,-100-float(index/100)*3});
            Check(node->AddComponent<IBoxColliderComponent>(),"Cannot create static collider");
        }
        Check(runtime->EndConstruction(error),error);Check(runtime->BeginExecution(error),error);
        PhysicsRaycastHit hit;PhysicsQuerySettings query;
        const auto ray=[&]{return runtime->Raycast(scene,{{0,5,0},{0,-1,0}},20,hit,query,error);};
        Check(ray()&&hit.Collider.ObjectID==ground->GetInstanceID()&&hit.Distance>4.4f&&hit.Distance<4.6f,"Initial static body pose invalid");
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<200;++frame)runtime->FixedUpdate(.02f);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        Check(provider.MaterialLoads==1,"Unchanged static scene reloaded its material");
        parent->GetTransform()->SetLocalPosition({0,2,0});
        Check(ray()&&hit.Distance>2.4f&&hit.Distance<2.6f,"Static parent transform remained stale");
        settings.Enabled=false;Check(collider->SetColliderSettings(settings),"Collider disable rejected");
        Check(!ray(),"Disabled static collider remained in the backend");
        settings.Enabled=true;Check(collider->SetColliderSettings(settings),"Collider enable rejected");
        Check(ray(),"Re-enabled static collider was not restored");
        parent->SetActive(false);Check(!ray(),"Inactive parent retained static collision");
        parent->SetActive(true);Check(ray(),"Active parent did not restore static collision");
        provider.Valid=false;++provider.Revision;
        Check(!runtime->SynchronizePhysics(error)&&error.find("material changed")!=std::string::npos,"Provider revision was hidden by the static cache");
        provider.Valid=true;++provider.Revision;Check(runtime->SynchronizePhysics(error),error);Check(ray(),"Failed synchronization poisoned the static cache");
        parent->GetTransform()->SetLocalPosition({});
        auto* ball=scene->CreateGameObject("New moving body");ball->GetTransform()->SetLocalPosition({0,4,0});
        Check(ball->AddComponent<ISphereColliderComponent>(),"Cannot add moving collider");
        auto* rigid=ball->AddComponent<IRigidbodyComponent>();Check(rigid&&rigid->SetEnabled(true),"Cannot enable new Rigidbody");
        for(unsigned frame=0;frame<30;++frame)runtime->FixedUpdate(.02f);
        Check(ball->GetTransform()->GetLocalPosition().Y<3,"New Rigidbody did not leave the static-only fast path");
        Check(runtime->EndExecution(error),error);runtime->SetResourceProvider(nullptr);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS static physics: 1001 colliders/200 fixed steps="<<elapsed<<"ms; transform/active/collider/provider revisions and dynamic transition\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
