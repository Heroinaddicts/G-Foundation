#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void Check(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
bool Near(float actual,float expected) {return std::fabs(actual-expected)<0.0001f;}
enum class SpawnMode {Normal,DuringUpdate,InactiveParent,DestroyParent,DestroyScene,RejectDestroyedParent,RejectDestroyedScene};
struct FixtureState {
    SpawnMode Mode=SpawnMode::Normal;
    GameSproutRuntime::IGameObject* InactiveParent=nullptr;
    std::uint64_t SceneID=0,OwnerID=0,ParentID=0,ChildID=0,NestedID=0,LegacyID=0,LaterID=0;
    int ParentInitialized=0,ChildConstructed=0,ChildCreated=0,ChildInitialized=0,ChildUpdated=0,ChildReleased=0,ChildDestroyed=0;
};
FixtureState State;
const GameSproutRuntime::ResourceReference MeshReference{"0123456789abcdef0123456789abcdef",3};
const GameSproutRuntime::ResourceReference MaterialReference{"1234567890abcdef1234567890abcdef",4};

class CreatedBehaviour final : public GameSproutRuntime::ScriptBehaviour {
public:
    CreatedBehaviour(){++State.ChildConstructed;}
    ~CreatedBehaviour() override {++State.ChildDestroyed;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_GameObject->Parent()!=nullptr,"Created script started before its node was attached");
        Check(_GameObject->GetComponent<GameSproutRuntime::IMeshComponent>()!=nullptr,"Created script cannot see configured Mesh");
        ++State.ChildCreated;
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        ++State.ChildInitialized;
        auto* later=runtime->CreateGameObject("Later grandchild",_GameObject);
        Check(later&&later->Parent()==nullptr,"Nested lifecycle creation bypassed the structure barrier");
        Check(later->AddComponent<GameSproutRuntime::IImageComponent>()!=nullptr,"Nested lifecycle component creation failed");
        State.LaterID=later->GetInstanceID();
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++State.ChildUpdated;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++State.ChildReleased;}
};
class CreatingBehaviour final : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        ++State.ParentInitialized;
        if(State.Mode!=SpawnMode::DuringUpdate) Spawn(runtime);
    }
    void Update(GameSproutRuntime::IGameSproutRuntime* const runtime,float) override {
        if(State.Mode==SpawnMode::DuringUpdate&&!State.ChildID) Spawn(runtime);
    }
private:
    void Spawn(GameSproutRuntime::IGameSproutRuntime* const runtime) {
        auto* scene=_GameObject->GetScene();
        if(State.Mode==SpawnMode::RejectDestroyedParent||State.Mode==SpawnMode::RejectDestroyedScene) {
            if(State.Mode==SpawnMode::RejectDestroyedParent) Check(scene->DestroyGameObject(_GameObject),"Parent destroy request failed");
            else Check(runtime->DestroyScene(scene),"Scene destroy request failed");
            Check(!runtime->CreateGameObject("Rejected child",_GameObject),"Creation accepted an unavailable parent in OnInitialized");
            Check(!scene->CreateGameObject("Rejected legacy child",_GameObject),"Scene creation accepted an unavailable parent");
            return;
        }
        auto* parent=State.Mode==SpawnMode::InactiveParent?State.InactiveParent:_GameObject;
        State.ParentID=parent->GetInstanceID();
        const auto oldCount=parent->GetChildCount();
        auto* child=runtime->CreateGameObject("Created child",parent);
        Check(child&&!child->GetScene(),"Deferred creation exposed scene ownership before its parent attachment committed");
        State.ChildID=child->GetInstanceID();
        Check(child->Parent()==nullptr&&parent->GetChildCount()==oldCount,"Creation mutated a hierarchy while its callback was active");
        Check(runtime->QueryGameObject(State.ChildID)==child,"Created node was unavailable for immediate configuration");
        auto* mesh=child->AddComponent<GameSproutRuntime::IMeshComponent>();
        auto* renderer=child->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
        auto* image=child->AddComponent<GameSproutRuntime::IImageComponent>();
        Check(mesh&&renderer&&image,"Cannot attach builtins to the newly returned node");
        Check(child->AddComponent<GameSproutRuntime::IMeshComponent>()==mesh,"New-node component deduplication failed");
        mesh->SetMesh(MeshReference);Check(renderer->SetMaterial(0,MaterialReference),"New-node material assignment failed");
        image->SetColor({0.1f,0.2f,0.3f,0.4f});
        child->GetTransform()->SetLocalPosition({1,2,3});
        child->GetTransform()->SetLocalScale({2,3,4});
        Check(child->GetComponentCount()==4,"Unexpected builtin component count");
        auto* nested=runtime->CreateGameObject("Immediate grandchild",child);
        Check(nested&&nested->Parent()==nullptr&&child->GetChildCount()==0,"Nested pending creation bypassed the callback barrier");
        State.NestedID=nested->GetInstanceID();
        auto* legacy=scene->CreateGameObject("Legacy child",parent);
        Check(legacy&&legacy->Parent()==nullptr,"Scene and Runtime creation have different callback semantics");
        State.LegacyID=legacy->GetInstanceID();
        auto* script=child->AddComponent<CreatedBehaviour>();
        Check(script&&child->GetComponent<CreatedBehaviour>()==script,"Cannot attach/query a prepared native component on the new node");
        Check(!child->AddComponent<CreatedBehaviour>(),"Created node accepted duplicate native behaviour");
        Check(State.ChildCreated==0&&State.ChildInitialized==0&&State.ChildUpdated==0,"Child callbacks ran before the startup construction phase ended");
        if(State.Mode==SpawnMode::DestroyParent) Check(scene->DestroyGameObject(parent),"Queued parent deletion failed");
        if(State.Mode==SpawnMode::DestroyScene) Check(runtime->DestroyScene(scene),"Queued scene deletion failed");
    }
};

GameSproutRuntime::IScene* Prepare(GameSproutRuntime::IGameSproutRuntime& runtime,SpawnMode mode,
                                 const GameSproutRuntime::ScriptTypeRegistration& registration) {
    State={};State.Mode=mode;
    auto* scene=runtime.CreateScene("Creation lifecycle");Check(scene,"Cannot create fixture scene");
    State.SceneID=scene->GetInstanceID();
    if(mode==SpawnMode::InactiveParent) {
        State.InactiveParent=runtime.CreateGameObject("Inactive target",scene);
        Check(State.InactiveParent,"Cannot create inactive target");State.InactiveParent->SetActive(false);
    }
    auto* owner=runtime.CreateGameObject("Creator",scene);Check(owner,"Cannot create script owner");
    owner->GetTransform()->SetLocalPosition({10,0,0});
    State.OwnerID=owner->GetInstanceID();
    std::string error;
    Check(runtime.AddScriptComponent(owner,registration.Info.TypeID,registration.Source,0,error)!=nullptr,error);
    Check(runtime.BeginExecution(error),error);
    return scene;
}
void CheckConfiguredChild(GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* parent=runtime.QueryGameObject(State.ParentID);
    auto* child=runtime.QueryGameObject(State.ChildID);
    auto* nested=runtime.QueryGameObject(State.NestedID);
    auto* legacy=runtime.QueryGameObject(State.LegacyID);
    Check(parent&&child&&nested&&legacy,"Deferred creation lost nodes");
    Check(child->GetScene()==parent->GetScene()&&nested->GetScene()==parent->GetScene()&&legacy->GetScene()==parent->GetScene(),
          "Committed creation did not publish the complete subtree's scene ownership");
    Check(child->Parent()==parent&&nested->Parent()==child&&legacy->Parent()==parent,"Deferred nodes were attached to the wrong parents");
    Check(parent->GetChildCount()==2&&parent->FindChild("Created child")==child,"Parent's committed child list is wrong");
    const auto position=child->GetTransform()->GetLocalPosition(),scale=child->GetTransform()->GetLocalScale();
    Check(Near(position.X,1)&&Near(position.Y,2)&&Near(position.Z,3)&&Near(scale.X,2)&&Near(scale.Y,3)&&Near(scale.Z,4),"Child configuration was lost during attachment");
    Check(child->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==MeshReference,"Mesh identity was not retained");
    Check(child->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterial(0)==MaterialReference,"Material identity was not retained");
    Check(Near(child->GetComponent<GameSproutRuntime::IImageComponent>()->GetColor().A,0.4f),"Image configuration was not retained");
}
void CheckInvalidParents(GameSproutRuntime::IGameSproutRuntime& runtime,const char* directory) {
    auto* scene=runtime.CreateScene("Invalid parents");Check(scene,"Invalid-parent scene creation failed");
    auto* parent=runtime.CreateGameObject("Parent",scene);Check(parent&&parent->Parent()==scene,"Scene-root parent was not accepted");
    auto* temporary=runtime.CreateGameObject("No explicit scene",nullptr);
    Check(temporary&&!temporary->Parent()&&!temporary->GetScene(),"Null parent did not create an unowned temporary object");
    const auto temporaryID=temporary->GetInstanceID();
    std::string frameError;Check(runtime.EndFrame(frameError),frameError);
    Check(!runtime.QueryGameObject(temporaryID),"Unattached object survived the frame boundary");
    auto* foreign=Export(directory);Check(foreign,"Cannot create second Runtime");
    try {
        auto* foreignScene=foreign->CreateScene("Foreign");
        auto* foreignParent=foreignScene->CreateGameObject("Foreign parent");
        Check(!runtime.CreateGameObject("Foreign scene",foreignScene)&&!runtime.CreateGameObject("Foreign object",foreignParent),"Runtime accepted a foreign-owned parent");
        Check(scene->GetChildCount()==1&&foreignScene->GetChildCount()==1&&foreignParent->GetChildCount()==0,"Rejected foreign parent changed either world");
    } catch(...) {delete foreign;throw;}
    delete foreign;
    bool visited=false;
    const auto parentID=parent->GetInstanceID();
    parent->ForeachComponent([&](GameSproutRuntime::IComponent*) {
        if(visited)return;visited=true;
        Check(scene->DestroyGameObject(parent),"Cannot mark a parent for deferred deletion");
        Check(!runtime.CreateGameObject("Dying parent",parent),"Runtime accepted a pending-destroy parent");
    });
    Check(visited&&!runtime.QueryGameObject(parentID)&&scene->GetChildCount()==0,"Invalid-parent fixture cleanup failed");
    Check(runtime.DestroyScene(scene),"Invalid-parent scene cleanup failed");
}
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try {
        Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<CreatingBehaviour>("object-creator-fixture","CreatingBehaviour",__FILE__,{"11111111111111111111111111111111",1},{});
        GameSproutRuntime::Internal::RegisterScriptClass<CreatedBehaviour>("created-object-fixture","CreatedBehaviour",__FILE__,{"22222222222222222222222222222222",1},{});
        const auto creator=GameSproutRuntime::Internal::ScriptModuleTypes()[0];
        std::string error;
        for(const auto& registration:GameSproutRuntime::Internal::ScriptModuleTypes())Check(runtime->RegisterScriptType(registration,error),error);
        CheckInvalidParents(*runtime,argv[1]);

        auto* scene=Prepare(*runtime,SpawnMode::Normal,creator);CheckConfiguredChild(*runtime);
        Check(State.ParentInitialized==1&&State.ChildCreated==1&&State.ChildInitialized==1&&State.ChildUpdated==0,
              "BeginExecution did not finish the created child's initialization before allowing gameplay");
        runtime->Update(0.01f);
        Check(State.ChildCreated==1&&State.ChildInitialized==1&&State.ChildUpdated==1,"Created script was initialized twice or missed its first subsequent tick");
        Check(runtime->QueryGameObject(State.LaterID)&&runtime->QueryGameObject(State.LaterID)->Parent()==runtime->QueryGameObject(State.ChildID),"Later child lifecycle creation was not committed");
        const auto childID=State.ChildID,nestedID=State.NestedID,laterID=State.LaterID;
        Check(runtime->EndExecution(error),error);
        Check(State.ChildReleased==1&&State.ChildDestroyed==1,"Created script was not released exactly once");
        Check(runtime->DestroyScene(scene)&&!runtime->QueryGameObject(childID)&&!runtime->QueryGameObject(nestedID)&&!runtime->QueryGameObject(laterID),"Scene destruction left runtime-created nodes");

        scene=Prepare(*runtime,SpawnMode::DuringUpdate,creator);
        Check(!State.ChildID&&State.ParentInitialized==1,"Ordinary-update fixture spawned during initial startup");
        runtime->Update(0.01f);CheckConfiguredChild(*runtime);
        Check(State.ChildConstructed==1&&State.ChildCreated==1&&State.ChildInitialized==1&&State.ChildUpdated==0,
              "An ordinary-update child did not initialize after attachment or was updated in its creation tick");
        runtime->Update(0.01f);
        Check(State.ChildCreated==1&&State.ChildInitialized==1&&State.ChildUpdated==1,
              "The deferred ordinary-update child did not start on its next tick");
        Check(runtime->EndExecution(error)&&runtime->DestroyScene(scene),"Ordinary-update fixture cleanup failed");

        scene=Prepare(*runtime,SpawnMode::InactiveParent,creator);CheckConfiguredChild(*runtime);
        runtime->Update(0.01f);runtime->FixedUpdate(0.01f);
        Check(State.ChildCreated==0&&State.ChildInitialized==0&&State.ChildUpdated==0,"Child of an inactive parent executed after attachment");
        runtime->QueryGameObject(State.ParentID)->SetActive(true);runtime->Update(0.01f);
        Check(State.ChildCreated==1&&State.ChildInitialized==1&&State.ChildUpdated==1,"Child did not start when its parent became active");
        Check(runtime->EndExecution(error)&&runtime->DestroyScene(scene),"Inactive fixture cleanup failed");

        for(const auto mode:{SpawnMode::DestroyParent,SpawnMode::DestroyScene,SpawnMode::RejectDestroyedParent,SpawnMode::RejectDestroyedScene}) {
            scene=Prepare(*runtime,mode,creator);
            Check(!runtime->QueryGameObject(State.OwnerID),"Deleted creator survived its initialization operation");
            // A child whose pending attachment lost its parent is still a
            // configurable temporary object until the explicit frame tail.
            // It must not initialize or update without a live scene root.
            if(State.ChildID) {
                Check(State.ChildInitialized==0&&State.ChildUpdated==0,"Failed pending attachment entered gameplay");
                for(const auto id:{State.ChildID,State.NestedID,State.LegacyID})
                    if(auto* temporary=runtime->QueryGameObject(id))Check(!temporary->GetScene(),"Failed pending attachment retained a destroyed scene");
            }
            Check(runtime->EndFrame(error),error);
            Check(!State.ChildID||(!runtime->QueryGameObject(State.ChildID)&&!runtime->QueryGameObject(State.NestedID)&&!runtime->QueryGameObject(State.LegacyID)),"Frame tail leaked unattached creation after owner/scene deletion");
            if(State.ChildID)Check(State.ChildReleased==1&&State.ChildDestroyed==1,"Prepared child behaviour was released incorrectly during startup deletion");
            Check(runtime->EndExecution(error),error);
            if(mode==SpawnMode::DestroyParent||mode==SpawnMode::RejectDestroyedParent)Check(runtime->DestroyScene(scene),"Surviving scene cleanup failed");
            Check(runtime->GetSceneCount()==0,"Creation lifecycle fixture left an extra scene");
        }
        delete runtime;runtime=nullptr;
        std::cout<<"PASS Runtime object creation, OnInitialized component/TRS configuration, deferred hierarchy, inactive parents, nested script startup and destruction cleanup\n";
    } catch(const std::exception& failure) {std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
