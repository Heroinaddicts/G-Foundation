#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeColliderMath.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
bool Near(float a,float b){return std::fabs(a-b)<.0002f;}
bool Near(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){return Near(a.X,b.X)&&Near(a.Y,b.Y)&&Near(a.Z,b.Z);}
class Provider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    GameSproutRuntime::Bounds Local{{.25f,1.5f,-.5f},{1,2,3}};
    bool Missing=false,Failure=false;unsigned Reads=0;
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string& error)override {
        ++Reads;data={};error.clear();if(Failure){error="fixture resource failure";return false;}
        if(!Missing)data.LocalBounds=Local;return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
};
}
int main(int argc,char** argv) {
    using namespace GameSproutRuntime;
    IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");
        Provider provider;runtime->SetResourceProvider(&provider);auto* scene=runtime->CreateScene("Collider fitting");
        const ResourceReference resource{"1349119f706a432ea716f963a18c6001",1};
        auto* owner=scene->CreateGameObject("Model");owner->GetTransform()->SetLocalScale({3,3,3});
        owner->AddComponent<IMeshComponent>()->SetMesh(resource);
        auto* box=owner->AddComponent<IBoxColliderComponent>();auto* sphere=owner->AddComponent<ISphereColliderComponent>();
        auto* capsule=owner->AddComponent<ICapsuleColliderComponent>();
        Check(box&&sphere&&capsule,"Default collider creation failed");
        Check(Near(box->GetShape().Size,{2,4,6})&&Near(box->GetColliderSettings().Center,provider.Local.Center),"Box did not fit local bounds, or multiplied Transform scale twice");
        Check(Near(sphere->GetShape().Radius,3)&&Near(sphere->GetColliderSettings().Center,provider.Local.Center),"Sphere did not use the longest local half-extent");
        Check(Near(capsule->GetShape().Radius,3)&&Near(capsule->GetShape().Height,6)&&capsule->GetShape().Axis==PhysicsAxis::Y,"Capsule did not default to Y and clamp height to diameter");
        auto* meshCollider=owner->AddComponent<IMeshColliderComponent>();ColliderFitResult fit;std::string error;
        Check(meshCollider->CalculateFit(fit,error)&&fit.HasBounds&&fit.Mesh.Mesh==resource&&Near(fit.Center,{}),"Mesh fit must bind owner mesh without doubling its local center: "+error);
        Check(box->SetShape({{7,8,9}}),"Manual shape");auto settings=box->GetColliderSettings();settings.Center={2,3,4};settings.IsTrigger=true;
        settings.Material={"1349119f706a432ea716f963a18c6002",7};Check(box->SetColliderSettings(settings),"Manual settings");
        Check(box->CalculateFit(fit,error)&&fit.HasBounds&&Near(fit.Box.Size,{2,4,6}),error);
        Check(Near(box->GetShape().Size,{7,8,9})&&Near(box->GetColliderSettings().Center,{2,3,4})&&box->GetColliderSettings().Material==settings.Material&&box->GetColliderSettings().IsTrigger,"Read-only fit changed authored values");
        const auto reads=provider.Reads;runtime->Update(.016f);runtime->Update(.016f);
        Check(provider.Reads==reads&&Near(box->GetShape().Size,{7,8,9}),"Update re-fit or re-read the model after authoring");
        // No recursive fitting: a child Renderer cannot silently size a group root.
        auto* group=scene->CreateGameObject("Group");auto* child=scene->CreateGameObject("Child model",group);child->AddComponent<IMeshComponent>()->SetMesh(resource);
        auto* empty=group->AddComponent<IBoxColliderComponent>();
        Check(Near(empty->GetShape().Size,{1,1,1})&&empty->CalculateFit(fit,error)&&!fit.HasBounds,"Default fit unexpectedly traversed descendants");
        provider.Local.Extents={2,1,.5f};settings=sphere->GetColliderSettings();settings.Rotation={0,0,std::sin(.3926990817f),std::cos(.3926990817f)};
        sphere->SetColliderSettings(settings);Check(sphere->CalculateFit(fit,error)&&Near(fit.Sphere.Radius,2),"Sphere rotation changed its fitted radius");
        settings=box->GetColliderSettings();settings.Rotation={0,0,.70710678f,.70710678f};box->SetColliderSettings(settings);
        Check(box->CalculateFit(fit,error)&&Near(fit.Box.Size,{2,4,1})&&Near(fit.Center,provider.Local.Center),"Rotated Box fit did not convert bounds into collider axes");
        provider.Missing=true;Check(box->CalculateFit(fit,error)&&!fit.HasBounds&&Near(box->GetShape().Size,{7,8,9}),"Missing mesh changed an existing shape");provider.Missing=false;
        provider.Local.Extents.X=-1;Check(!box->CalculateFit(fit,error)&&!error.empty()&&Near(box->GetShape().Size,{7,8,9}),"Invalid bounds accepted or changed authored properties");
        provider.Local.Extents={1,2,3};provider.Failure=true;auto* fallback=owner->AddComponent<IBoxColliderComponent>();Check(fallback&&Near(fallback->GetShape().Size,{1,1,1}),"Unavailable provider prevented default collider creation");provider.Failure=false;
        Check(runtime->DestroyScene(scene),"Destroy fit scene");runtime->SetResourceProvider(nullptr);
        scene=runtime->CreateScene("Scaled physical shapes");
        auto* builtin=scene->CreateGameObject("Builtin");builtin->AddComponent<IMeshComponent>()->SetMesh(GetBuiltinMeshReference(PrimitiveType::Cube));
        auto* builtinBox=builtin->AddComponent<IBoxColliderComponent>();Check(builtinBox&&Near(builtinBox->GetShape().Size,{1,1,1}),"Builtin fitting required a provider");
        auto* ball=scene->CreateGameObject("Scaled sphere");ball->GetTransform()->SetLocalPosition({10,0,0});ball->GetTransform()->SetLocalScale({-2,3,4});auto* ballCollider=ball->AddComponent<ISphereColliderComponent>();
        settings=ballCollider->GetColliderSettings();settings.Rotation={0,0,std::sin(.3926990817f),std::cos(.3926990817f)};ballCollider->SetColliderSettings(settings);
        auto* pill=scene->CreateGameObject("Scaled capsule");pill->GetTransform()->SetLocalPosition({20,0,0});pill->GetTransform()->SetLocalScale({-2,3,4});auto* pillCollider=pill->AddComponent<ICapsuleColliderComponent>();
        PhysicsRaycastHit hit;PhysicsQuerySettings query;
        Check(runtime->Raycast(scene,{{10,5,0},{0,-1,0}},10,hit,query,error)&&Near(hit.Distance,3),"Nonuniform sphere physical radius differs from max-axis rule: "+error);
        Check(runtime->Raycast(scene,{{20,5,0},{0,-1,0}},10,hit,query,error)&&Near(hit.Distance,2),"Nonuniform capsule physical height differs from axis/perpendicular rules: "+error);
        auto shape=pillCollider->GetShape();shape.Axis=PhysicsAxis::X;pillCollider->SetShape(shape);
        Check(runtime->Raycast(scene,{{25,0,0},{-1,0,0}},10,hit,query,error)&&Near(hit.Distance,3),"Capsule axis change did not recompute scaled diameter clamp: "+error);
        RuntimePhysicsMath::ColliderFrame frame;
        Check(!RuntimePhysicsMath::TryGetColliderFrame(RuntimeMath::ComposeTRS({},{},{0,1,1}),frame),"Zero scale frame accepted");
        Mat4 shear;shear(0,1)=.25f;Check(!RuntimePhysicsMath::TryGetColliderFrame(shear,frame),"Sheared primitive frame accepted");
        delete runtime;runtime=nullptr;std::cout<<"PASS Collider default fit/read-only fit, scope, authored values, resource errors, signed nonuniform physical sphere/capsule and invalid frame guards\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
