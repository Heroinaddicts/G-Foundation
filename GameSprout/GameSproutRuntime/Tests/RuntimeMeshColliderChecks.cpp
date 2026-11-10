#include "GameSproutRuntimeExport.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeResourceApi.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
void Check(bool value,const std::string &error){if(!value)throw std::runtime_error(error);}
class Meshes final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    uint64_t Revision=1,MeshRevision=1;unsigned Reads=0,PreparedReads=0;
    GameSproutRuntime::MeshRenderData Mesh;
    GameSproutRuntime::CookedMeshCollisionData Prepared;
    Meshes() {
        const float points[]={-1,0,-1,1,0,-1,0,0,1,0,2,0};
        const uint32_t indices[]={0,1,2,0,3,1,1,3,2,2,3,0};
        Mesh.VertexCount=4;Mesh.VertexStride=12;Mesh.IndexCount=12;Mesh.IndexOffset=sizeof(points);
        Mesh.Bytes.resize(sizeof(points)+sizeof(indices));std::memcpy(Mesh.Bytes.data(),points,sizeof(points));std::memcpy(Mesh.Bytes.data()+sizeof(points),indices,sizeof(indices));
    }
    uint64_t GetRevision()const override{return Revision;}
    uint64_t GetResourceRevision(const GameSproutRuntime::ResourceReference &)const override{return MeshRevision;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference &,GameSproutRuntime::MeshRenderData &output,std::string &error)override{++Reads;output=Mesh;error.clear();return true;}
    bool LoadMeshCollision(const GameSproutRuntime::ResourceReference &,bool convex,std::vector<std::uint8_t>& output,
                           std::string &error)override {
        ++PreparedReads;output=convex?Prepared.Convex:Prepared.Triangle;error.clear();return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference &,GameSproutRuntime::TextureRenderData &,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference &,GameSproutRuntime::MaterialRenderData &,std::string&)override{return false;}
};
void CheckPreparedCollision(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    Meshes resources;
    const auto cooker=runtime.GetMeshCollisionCooker();
    const auto& mesh=resources.Mesh;
    Check(cooker.Cook&&cooker.Format&&cooker.Cook({mesh.Bytes,mesh.VertexCount,mesh.VertexStride,mesh.IndexCount,
        mesh.PositionOffset,mesh.IndexOffset},resources.Prepared,error)&&!resources.Prepared.Triangle.empty()&&
        !resources.Prepared.Convex.empty(),"Cannot cook native tetrahedron: "+error);
    runtime.SetResourceProvider(&resources);
    auto* scene=runtime.CreateScene("Offline collision geometry");
    const GameSproutRuntime::ResourceReference reference{"11111111111111111111111111111111",1};
    auto* triangle=scene->CreateGameObject("Uniform scaled triangle mesh");
    triangle->GetTransform()->SetLocalScale({2,2,2});
    auto* triangleCollider=triangle->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
    Check(triangleCollider->SetShape({reference,false}),"Assign offline triangle mesh");
    auto* compound=scene->CreateGameObject("Signed scale compound");
    compound->GetTransform()->SetLocalPosition({10,0,0});
    compound->GetTransform()->SetLocalScale({-2,1,3});
    auto* convex=compound->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
    auto* box=compound->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    auto boxSettings=box->GetColliderSettings();boxSettings.Center={-2,0,0};
    Check(convex->SetShape({reference,true})&&box->SetColliderSettings(boxSettings),"Assign offline compound leaves");
    GameSproutRuntime::PhysicsRaycastHit hit;GameSproutRuntime::PhysicsQuerySettings query;
    Check(runtime.Raycast(scene,{{0,8,-.25f},{0,-1,0}},20,hit,query,error)&&
        hit.Collider.ComponentID==triangleCollider->GetComponentInstanceID()&&std::fabs(hit.Position.Y-3.5f)<.001f,
        "Scaled offline triangle geometry changed: "+error);
    Check(runtime.Raycast(scene,{{10,8,0},{0,-1,0}},20,hit,query,error)&&
        hit.Collider.ComponentID==convex->GetComponentInstanceID()&&std::fabs(hit.Position.Y-2)<.001f,
        "Offline convex compound geometry changed: "+error);
    Check(resources.Reads==0&&resources.PreparedReads==2,"Prepared colliders read raw bytes or restored twice");
    ++resources.Revision;
    Check(runtime.Raycast(scene,{{10,8,0},{0,-1,0}},20,hit,query,error)&&resources.PreparedReads==2,
        "Unrelated import invalidated native collision data");
    resources.Prepared.Convex[0]^=0xff;
    ++resources.MeshRevision;++resources.Revision;
    Check(runtime.Raycast(scene,{{10,8,0},{0,-1,0}},20,hit,query,error)&&resources.Reads==1&&
        resources.PreparedReads==4&&std::fabs(hit.Position.Y-2)<.001f,
        "Malformed cooked payload did not fall back to current raw geometry: "+error);
    Check(runtime.DestroyScene(scene),"Destroy offline collision fixture");
    runtime.SetResourceProvider(nullptr);
}
}
int main(int argc,char **argv) {
    GameSproutRuntime::IGameSproutRuntime *runtime=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime load");std::string error;
        CheckPreparedCollision(*runtime);
        auto *scene=runtime->CreateScene("Mesh physics");
        auto *floor=scene->CreateGameObject("Builtin plane");
        auto *mesh=floor->AddComponent<GameSproutRuntime::IMeshComponent>();mesh->SetMesh(GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Plane));
        auto *collider=floor->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(collider&&collider->GetShape().Mesh==mesh->GetMesh()&&!collider->GetShape().Convex,"MeshCollider did not adopt owner mesh");
        auto *second=floor->AddComponent<GameSproutRuntime::IMeshColliderComponent>();Check(second&&second!=collider,"MeshCollider did not allow multiple components");
        Check(floor->RemoveComponentInstance(second->GetComponentInstanceID()),"Remove extra collider");
        GameSproutRuntime::PhysicsRaycastHit hit;GameSproutRuntime::PhysicsQuerySettings query;
        Check(runtime->Raycast(scene,{{0,4,0},{0,-1,0}},10,hit,query,error)&&hit.Collider.ComponentID==collider->GetComponentInstanceID(),"Builtin plane failed without a provider: "+error);
        floor->GetTransform()->SetLocalScale({-2,1,3});
        Check(runtime->Raycast(scene,{{0,4,0},{0,-1,0}},10,hit,query,error)&&hit.Normal.Y>.99f,"Mirrored triangle plane inverted front face: "+error);
        // Rotate a +Y plane into a +Z-facing wall. Unity's +Z forward must not
        // introduce a second winding flip at the Runtime -> Jolt boundary.
        auto* wall=scene->CreateGameObject("Positive Z triangle face");
        wall->GetTransform()->SetLocalPosition({0,3,5});
        const float halfTurn=std::sqrt(.5f);
        Check(wall->GetTransform()->SetLocalRotation({halfTurn,0,0,halfTurn}),"Cannot rotate triangle wall");
        auto* wallCollider=wall->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(wallCollider&&wallCollider->SetShape(collider->GetShape()),"Cannot assign triangle wall mesh");
        for(const float xScale:{1.f,-1.f}) {
            wall->GetTransform()->SetLocalScale({xScale,1,1});
            Check(runtime->Raycast(scene,{{0,3,8},{0,0,-1}},2.9f,hit,query,error)==false,
                  "Triangle ray unexpectedly hit before its plane");
            Check(runtime->Raycast(scene,{{0,3,8},{0,0,-1}},4,hit,query,error)&&
                  hit.Collider.ComponentID==wallCollider->GetComponentInstanceID()&&hit.Normal.Z>.99f,
                  "Positive Z triangle face or mirrored face normal changed sign: "+error);
            Check(!runtime->Raycast(scene,{{0,3,2},{0,0,1}},4,hit,query,error)&&error.empty(),
                  "Triangle back face became solid after changing world convention: "+error);
        }
        Check(scene->DestroyGameObject(wall),"Cannot destroy triangle wall fixture");
        // A rotated child of a non-uniform parent has shear, baked into static triangles.
        auto *parent=scene->CreateGameObject("Scaled parent");parent->GetTransform()->SetLocalScale({2,1,3});
        auto *sheared=scene->CreateGameObject("Sheared triangles",parent);sheared->GetTransform()->SetLocalPosition({10,0,0});
        sheared->GetTransform()->SetLocalRotation(GameSproutRuntime::Quaternion{0,std::sin(.35f),0,std::cos(.35f)});
        auto *shearedCollider=sheared->AddComponent<GameSproutRuntime::IMeshColliderComponent>();Check(shearedCollider->SetShape(collider->GetShape()),"Set sheared mesh");
        Check(runtime->Raycast(scene,{{20,4,0},{0,-1,0}},10,hit,query,error),"Static mesh affine baking failed: "+error);
        Check(scene->DestroyGameObject(parent),"Destroy shear fixture");
        Meshes resources;runtime->SetResourceProvider(&resources);
        const GameSproutRuntime::ResourceReference reference{"11111111111111111111111111111111",1};
        auto* staticScene=runtime->CreateScene("Static mesh revision");
        auto* staticMesh=staticScene->CreateGameObject("Prepared static mesh")->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(staticMesh->SetShape({reference,false}),"Cannot assign static revision fixture");
        Check(runtime->Raycast(staticScene,{{0,8,0},{0,-1,0}},20,hit,query,error),error);
        const float oldHeight=hit.Position.Y;
        ++resources.Revision;
        Check(runtime->Raycast(staticScene,{{0,8,0},{0,-1,0}},20,hit,query,error)&&resources.Reads==1,
              "Unrelated import invalidated a stable static mesh snapshot");
        const auto originalBytes=resources.Mesh.Bytes;
        for(unsigned vertex=0;vertex<resources.Mesh.VertexCount;++vertex) {
            float height=0;auto* bytes=resources.Mesh.Bytes.data()+vertex*resources.Mesh.VertexStride+4;
            std::memcpy(&height,bytes,sizeof(height));height+=3;std::memcpy(bytes,&height,sizeof(height));
        }
        ++resources.MeshRevision;++resources.Revision;
        Check(runtime->Raycast(staticScene,{{0,8,0},{0,-1,0}},20,hit,query,error)&&resources.Reads==2&&std::fabs(hit.Position.Y-oldHeight-3)<.001f,
              "Relevant mesh revision did not invalidate the static world and collision geometry");
        Check(runtime->DestroyScene(staticScene),"Cannot destroy static revision fixture");
        resources.Mesh.Bytes=originalBytes;++resources.MeshRevision;++resources.Revision;resources.Reads=0;
        auto *actor=scene->CreateGameObject("Convex mesh");actor->GetTransform()->SetLocalPosition({0,4,0});
        auto *body=actor->AddComponent<GameSproutRuntime::IRigidbodyComponent>();body->SetConstraints(GameSproutRuntime::PhysicsConstraint::FreezeRotation);
        auto *actorCollider=actor->AddComponent<GameSproutRuntime::IMeshColliderComponent>();
        Check(actorCollider->GetShape().Convex,"Dynamic MeshCollider default must be convex");
        Check(actorCollider->SetShape({reference,true}),"Assign prepared mesh");
        unsigned collisions=0;actorCollider->RegisterCollisionEnter([&](const auto &event){if(event.Other.ComponentID==collider->GetComponentInstanceID())++collisions;});
        Check(runtime->BeginExecution(error),error);
        for(unsigned i=0;i<150;++i)runtime->FixedUpdate(1.f/60);
        Check(actor->GetTransform()->GetLocalPosition().Y>-.1f&&actor->GetTransform()->GetLocalPosition().Y<.1f&&collisions==1,"Convex mesh did not collide with triangle floor");
        Check(resources.Reads==1,"Mesh provider was re-read on each fixed step");
        ++resources.Revision;for(unsigned i=0;i<10;++i)runtime->FixedUpdate(1.f/60);
        Check(resources.Reads==1&&collisions==1,"Unrelated resource import reloaded collision geometry or recreated a contacting body");
        body->Teleport({1,3,1},{},true);runtime->FixedUpdate(1.f/60);Check(resources.Reads==1,"Rigid body move re-read mesh bytes");
        Check(runtime->EndExecution(error),error);
        auto shape=actorCollider->GetShape();shape.Convex=false;Check(actorCollider->SetShape(shape),"Set nonconvex mesh");
        Check(!runtime->BeginExecution(error)&&error.find("Non-convex")!=std::string::npos,"Dynamic triangle mesh should be rejected");
        Check(body->SetMotionType(GameSproutRuntime::PhysicsMotionType::Kinematic),"Set kinematic");
        Check(runtime->BeginExecution(error),"Kinematic triangle mesh failed: "+error);Check(runtime->EndExecution(error),error);
        auto settings=actorCollider->GetColliderSettings();settings.IsTrigger=true;Check(actorCollider->SetColliderSettings(settings),"Configure trigger");
        Check(!runtime->BeginExecution(error)&&!error.empty(),"Nonconvex trigger should be rejected");
        settings.IsTrigger=false;actorCollider->SetColliderSettings(settings);shape.Convex=true;actorCollider->SetShape(shape);
        resources.Mesh.Bytes[0]=0; // Explicit invalid source revision must invalidate the decode cache.
        const uint32_t invalid=1000;std::memcpy(resources.Mesh.Bytes.data()+resources.Mesh.IndexOffset,&invalid,4);++resources.MeshRevision;++resources.Revision;
        Check(!runtime->BeginExecution(error)&&error.find("index")!=std::string::npos,"Invalid provider revision reused stale collision geometry");
        delete runtime;runtime=nullptr;std::cout<<"PASS MeshCollider builtin/provider, offline geometry, affine/mirror, convex dynamics, restrictions, revision cache and callbacks\n";return 0;
    }catch(const std::exception &error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
