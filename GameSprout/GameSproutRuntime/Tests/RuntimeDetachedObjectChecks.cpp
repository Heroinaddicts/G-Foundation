#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "Internal/RuntimeScriptBindings.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
const GameSproutRuntime::ResourceReference MeshReference{"0123456789abcdef0123456789abcdef",7};
struct Counters {int Created=0,Initialized=0,Updated=0,Fixed=0,Released=0,Destroyed=0;};
Counters Counts;
class Probe final : public GameSproutRuntime::ScriptBehaviour {
public:
    float Value=1;
    bool OpenScopeOnRelease=false;
    ~Probe() override {++Counts.Destroyed;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.Created;}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_GameObject->GetScene()!=nullptr,"OnInitialized ran without a live scene owner");
        Check(Value==41,"OnInitialized ran before field configuration completed");
        const auto* mesh=_GameObject->GetComponent<GameSproutRuntime::IMeshComponent>();
        Check(mesh&&mesh->GetMesh()==MeshReference,"OnInitialized ran before Mesh configuration completed");
        ++Counts.Initialized;
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Updated;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        ++Counts.Released;
        if(OpenScopeOnRelease) {
            std::string error;
            Check(runtime->BeginConstruction(error),error);
            throw std::runtime_error("Intentional cleanup construction scope leak");
        }
    }
};
class Events final : public GameSproutRuntime::IRuntimeListener {
public:
    std::vector<GameSproutRuntime::SceneNodeEvent> Values;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {Values.push_back(event);}
    bool Contains(std::uint64_t id) const {
        return std::any_of(Values.begin(),Values.end(),[id](const auto& event){return event.ObjectID==id;});
    }
};
class AnimationResources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override {return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&) override {return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override {return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&) override {return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&) override {
        data={};data.Nodes.push_back({"/Root#0","Root",-1,{}});data.Nodes.push_back({"/Root#0/Bone#0","Bone",0,{}});return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimationClipData& data,std::string&) override {
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={"/Root#0","/Root#0/Bone#0"};
        GameSproutRuntime::AnimationFrame start,end;start.Time=0;end.Time=1;start.Poses.resize(2);end.Poses.resize(2);
        end.Poses[1].Position.X=2;data.Frames={start,end};return true;
    }
};
GameSproutRuntime::IGameObject* CreateConfigured(GameSproutRuntime::IGameSproutRuntime& runtime,const char* name) {
    auto* object=runtime.CreateGameObject(name);
    Check(object,"Runtime could not create an object without a parent");
    Check(!object->Parent()&&!object->GetScene(),"New unowned object implicitly selected a scene");
    auto* behaviour=object->AddComponent<Probe>();
    Check(behaviour,"Cannot configure native script on an unowned object");behaviour->Value=41;
    auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();
    Check(mesh,"Cannot configure Mesh on an unowned object");mesh->SetMesh(MeshReference);
    Check(object->AddComponent<GameSproutRuntime::IMeshRendererComponent>(),"Cannot configure renderer before attachment");
    Check(Counts.Initialized==0,"Unowned object was initialized during component configuration");
    return object;
}
void Finish(GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IScene* scene=nullptr) {
    std::string error;
    Check(runtime.EndExecution(error),error);
    if(scene)Check(runtime.DestroyScene(scene),"Cannot destroy detached-object fixture scene");
    Check(runtime.EndFrame(error),error);
}
void CheckUnownedCollection(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    Check(runtime.GetSceneCount()==0&&runtime.BeginExecution(error),error);
    Events events;Check(runtime.RegisterListener(&events),"Cannot watch unowned-object events");
    auto* parent=runtime.CreateGameObject("Unowned parent");
    auto* child=CreateConfigured(runtime,"Unowned child");
    const auto parentID=parent->GetInstanceID(),childID=child->GetInstanceID();
    Check(parent->AddChild(child,false),"Cannot compose a temporary subtree");
    Check(child->Parent()==parent&&!child->GetScene(),"Temporary parent fabricated scene ownership");
    runtime.Update(.01f);runtime.FixedUpdate(.02f);runtime.FixedUpdate(.02f);runtime.Render();runtime.ProcessPendingEvents();
    Check(runtime.QueryGameObject(parentID)&&runtime.QueryGameObject(childID),"A tick/render reclaimed objects before the frame tail");
    Check(Counts.Initialized==0&&Counts.Updated==0&&Counts.Fixed==0,"Unowned subtree entered gameplay");
    Check(runtime.EndFrame(error),error);
    Check(!runtime.QueryGameObject(parentID)&&!runtime.QueryGameObject(childID),"A temporary parent incorrectly kept its child alive");
    Check(Counts.Released==1&&Counts.Destroyed==1,"Unowned native instance was not released/destroyed exactly once");
    Check(!events.Contains(parentID)&&!events.Contains(childID),"Unowned objects leaked scene create/destroy events");
    Check(runtime.EndFrame(error)&&Counts.Released==1&&Counts.Destroyed==1,"Repeated frame-tail collection released objects twice");
    runtime.UnregisterListener(&events);Finish(runtime);
}
void CheckAttachedSubtree(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    auto* scene=runtime.CreateScene("Attach completed subtree");
    Check(scene&&runtime.BeginExecution(error),error);
    Events events;Check(runtime.RegisterListener(&events),"Cannot subscribe attachment events");
    auto* branch=runtime.CreateGameObject("Branch");
    auto* leaf=CreateConfigured(runtime,"Configured leaf");
    const auto branchID=branch->GetInstanceID(),leafID=leaf->GetInstanceID();
    Check(branch->AddChild(leaf,false),"Cannot build temporary subtree before choosing its scene");
    // Consume its pending initialization records while it is still unowned.
    // Adoption must register the entire subtree again, rather than losing it.
    runtime.Update(.01f);runtime.FixedUpdate(.02f);runtime.ProcessPendingEvents();
    Check(Counts.Initialized==0&&Counts.Updated==0&&Counts.Fixed==0,"Temporary subtree entered simulation before adoption");
    Check(!events.Contains(branchID)&&!events.Contains(leafID),"Temporary subtree was visible in scene events");
    Check(scene->AddChild(branch,false),"Cannot attach a completed temporary subtree");
    Check(branch->GetScene()==scene&&leaf->GetScene()==scene&&leaf->Parent()==branch,"Attachment did not assign the entire subtree's scene");
    Check(runtime.EndFrame(error)&&runtime.QueryGameObject(branchID)&&runtime.QueryGameObject(leafID),"Attached subtree was reclaimed");
    Check(events.Contains(branchID)&&events.Contains(leafID),"Subtree attachment did not publish all scene nodes");
    Check(Counts.Initialized==0,"EndFrame advanced pending user lifecycle");
    runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(Counts.Initialized==1&&Counts.Updated==1&&Counts.Fixed==1,"Attached object did not initialize before its first gameplay tick");
    Check(runtime.EndFrame(error)&&runtime.QueryGameObject(leafID),"A rooted object was reclaimed on a later frame");
    runtime.UnregisterListener(&events);Finish(runtime,scene);
    Check(Counts.Released==1&&Counts.Destroyed==1,"Attached instance cleanup was not exactly once");
}
void CheckInactiveSurvival(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    auto* scene=runtime.CreateScene("Inactive ownership");Check(scene&&runtime.BeginExecution(error),error);
    scene->SetActive(false);
    auto* object=CreateConfigured(runtime,"Inactive rooted object");const auto id=object->GetInstanceID();
    Check(object->SetParent(scene,false),"SetParent did not attach an unowned object");
    runtime.Update(.01f);Check(runtime.EndFrame(error),error);
    Check(runtime.QueryGameObject(id)==object&&object->GetScene()==scene&&Counts.Initialized==0,"Inactive was confused with unowned/reclaimable");
    scene->SetActive(true);runtime.Update(.01f);
    Check(Counts.Initialized==1&&Counts.Updated==1,"Rooted inactive component did not initialize when activated");
    Finish(runtime,scene);
}
void CheckEscapeFromTemporaryParent(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    auto* scene=runtime.CreateScene("Escape temporary ancestor");Check(scene&&runtime.BeginExecution(error),error);
    auto* parent=runtime.CreateGameObject("Abandoned ancestor");
    auto* child=CreateConfigured(runtime,"Escaping subtree");
    auto* grandchild=runtime.CreateGameObject("Grandchild");
    const auto parentID=parent->GetInstanceID(),childID=child->GetInstanceID(),grandchildID=grandchild->GetInstanceID();
    Check(parent->AddChild(child,false)&&child->AddChild(grandchild,false),"Cannot compose temporary nested subtree");
    Check(scene->AddChild(child,false)&&parent->GetChildCount()==0,"Cannot rescue a subtree before the frame tail");
    runtime.Update(.01f);Check(runtime.EndFrame(error),error);
    Check(!runtime.QueryGameObject(parentID)&&runtime.QueryGameObject(childID)&&runtime.QueryGameObject(grandchildID),"Reclaiming the abandoned ancestor also reclaimed the rescued subtree");
    Check(child->GetScene()==scene&&grandchild->GetScene()==scene&&Counts.Initialized==1,"Rescued subtree did not inherit the live scene");
    Finish(runtime,scene);
}
void CheckDetachAndReattach(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    auto* scene=runtime.CreateScene("Detach and reattach");Check(scene&&runtime.BeginExecution(error),error);
    auto* object=CreateConfigured(runtime,"Surviving instance");const auto id=object->GetInstanceID();
    Check(scene->AddChild(object,false),"Cannot root detach fixture");runtime.Update(.01f);
    Check(Counts.Initialized==1&&Counts.Updated==1,"Detach fixture did not initialize");
    Check(object->SetParent(nullptr,true)&&!object->Parent()&&!object->GetScene(),"SetParent(nullptr) did not remove scene ownership");
    runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(Counts.Updated==1&&Counts.Fixed==0,"Detached initialized object continued simulation");
    auto* temporaryParent=runtime.CreateGameObject("Temporary new parent");const auto parentID=temporaryParent->GetInstanceID();
    Check(temporaryParent->AddChild(object,false),"Cannot attach a previously initialized object to a temporary ancestor");
    Check(!object->GetScene(),"An unowned parent retained its child's previous scene");
    Check(scene->AddChild(object,false)&&runtime.EndFrame(error),error);
    Check(!runtime.QueryGameObject(parentID)&&runtime.QueryGameObject(id)==object,"Reattachment did not cancel frame-tail collection");
    runtime.Update(.01f);runtime.FixedUpdate(.02f);
    Check(Counts.Initialized==1&&Counts.Updated==2&&Counts.Fixed==1&&Counts.Released==0,"Reattachment recreated or reinitialized the original instance");
    Check(scene->RemoveChild(object,true)&&!object->GetScene(),"RemoveChild still implicitly reparents to the scene root");
    Check(runtime.EndFrame(error)&&!runtime.QueryGameObject(id),"An object detached for the rest of the frame was not reclaimed");
    Check(Counts.Released==1&&Counts.Destroyed==1,"Detached ready instance cleanup ran more than once");
    Finish(runtime,scene);
}
void CheckCallbackBoundaries(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;
    auto* scene=runtime.CreateScene("Deferred attachment");Check(scene&&runtime.BeginExecution(error),error);
    auto* anchor=scene->CreateGameObject("Anchor");std::uint64_t attachedID=0,orphanID=0;
    scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
        auto* object=CreateConfigured(runtime,"Attached after traversal");attachedID=object->GetInstanceID();
        auto* orphan=runtime.CreateGameObject("Abandoned after traversal");orphanID=orphan->GetInstanceID();
        Check(anchor->AddChild(object,false),"Cannot queue attachment during a traversal");
        Check(!object->Parent(),"Queued attachment modified the active traversal");
        Check(!runtime.EndFrame(error)&&runtime.QueryGameObject(orphanID),"EndFrame collected objects inside a Runtime callback");
    });
    Check(runtime.QueryGameObject(attachedID)->Parent()==anchor,"Queued attachment did not commit after traversal");
    Check(runtime.EndFrame(error)&&runtime.QueryGameObject(attachedID)&&!runtime.QueryGameObject(orphanID),"Frame tail ignored the committed hierarchy");
    runtime.Update(.01f);Check(Counts.Initialized==1&&Counts.Updated==1,"Deferred attachment lost pending initialization");
    auto* dying=scene->CreateGameObject("Explicitly destroyed");const auto dyingID=dying->GetInstanceID();
    auto* dyingChild=scene->CreateGameObject("Already rooted child",dying);const auto dyingChildID=dyingChild->GetInstanceID();
    Events events;Check(runtime.RegisterListener(&events),"Cannot watch explicit destruction");bool visited=false;
    dying->ForeachComponent([&](GameSproutRuntime::IComponent*) {
        if(visited)return;visited=true;
        Check(scene->DestroyGameObject(dying),"Cannot request explicit destruction");
        Check(!dying->SetParent(nullptr,false)&&!anchor->AddChild(dying,false),"Parent changes resurrected an explicitly destroyed node");
    });
    Check(!runtime.QueryGameObject(dyingID)&&!runtime.QueryGameObject(dyingChildID),"Explicitly destroyed rooted subtree survived its safe boundary");
    runtime.ProcessPendingEvents();
    for(const auto id:{dyingID,dyingChildID})Check(std::any_of(events.Values.begin(),events.Values.end(),[&](const auto& event) {
        return event.ObjectID==id&&event.SceneID==scene->GetInstanceID()&&event.Type==GameSproutRuntime::SceneNodeEventType::Destroy;
    }),"Explicit rooted subtree destruction lost its original scene identity in the Destroy event");
    runtime.UnregisterListener(&events);
    Finish(runtime,scene);
}
void CheckTemporaryAnimator(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;AnimationResources resources;runtime.SetResourceProvider(&resources);
    auto* scene=runtime.CreateScene("Temporary Animator");Check(scene&&runtime.BeginExecution(error),error);
    auto* root=runtime.CreateGameObject("Unowned Animator");const auto rootID=root->GetInstanceID();
    auto* bone=runtime.CreateGameObject("Unowned bone");const auto boneID=bone->GetInstanceID();
    Check(root&&bone&&root->AddChild(bone,false),"Cannot build temporary skeleton hierarchy");
    auto* animator=root->AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(animator,"Cannot configure temporary Animator");
    animator->SetSkeleton({MeshReference.Guid,10});animator->SetDefaultClip({MeshReference.Guid,11});
    Check(animator->SetNodeBindings({{"/Root#0",rootID},{"/Root#0/Bone#0",boneID}}),"Cannot configure temporary skeleton bindings");
    Check(!animator->Play("Default")&&!animator->GetLastError().empty(),"Unowned Animator accepted gameplay playback");
    Check(!animator->PlayClip({MeshReference.Guid,11})&&!animator->GetLastError().empty(),"Unowned Animator accepted direct Clip playback");
    animator->Pause();animator->Stop();animator->StopClip();runtime.Update(.01f);
    Check(runtime.QueryGameObject(rootID)&&runtime.QueryGameObject(boneID),"Animator configuration prematurely reclaimed a temporary subtree");
    Check(scene->AddChild(root,false),"Cannot adopt the temporary Animator hierarchy");
    runtime.Update(0);
    Check(animator->Play("Default",.5f),animator->GetLastError());
    Check(bone->GetTransform()->GetWorldMatrix()(0,3)>.99f,"Adopted Animator did not evaluate its configured skeleton");
    animator->Stop();
    Check(root->SetParent(nullptr,true)&&!bone->GetScene(),"Animator detach did not clear descendant scene ownership");
    animator->Pause();animator->Stop();animator->StopClip();
    Check(!animator->Play("Default"),"Detached Animator restarted playback without a live scene");
    Check(runtime.EndFrame(error)&&!runtime.QueryGameObject(rootID)&&!runtime.QueryGameObject(boneID),"Temporary Animator/skeleton cleanup failed");
    runtime.SetResourceProvider(nullptr);Finish(runtime,scene);
}
void CheckReleaseConstructionFailure(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};std::string error;Check(runtime.BeginExecution(error),error);
    auto* object=CreateConfigured(runtime,"Cleanup failure");const auto id=object->GetInstanceID();
    auto* behaviour=object->GetComponent<Probe>();Check(behaviour,"Cannot configure the release failure fixture");
    behaviour->OpenScopeOnRelease=true;
    Check(!runtime.EndFrame(error)&&!error.empty(),"A cleanup callback's leaked construction scope was not reported");
    Check(!runtime.QueryGameObject(id)&&Counts.Released==1&&Counts.Destroyed==1&&Counts.Initialized==0,
          "Cleanup construction failure retained or double-released the unowned object");
    Check(!runtime.EndConstruction(error),"Cleanup failure retained a hidden construction scope");
    Check(runtime.BeginConstruction(error)&&runtime.EndConstruction(error),"Construction did not recover after a release callback failure");
    Check(runtime.EndExecution(error),error);
    Check(runtime.EndFrame(error)&&Counts.Released==1&&Counts.Destroyed==1,"A later frame repeated cleanup after the failed collection");
}
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try {
        Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<Probe>("detached-object-fixture","Probe",__FILE__,{"detached-source",1},{});
        std::string error;
        Check(runtime->RegisterScriptType(GameSproutRuntime::Internal::ScriptModuleTypes().back(),error),error);
        CheckUnownedCollection(*runtime);
        CheckAttachedSubtree(*runtime);
        CheckInactiveSurvival(*runtime);
        CheckEscapeFromTemporaryParent(*runtime);
        CheckDetachAndReattach(*runtime);
        CheckCallbackBoundaries(*runtime);
        CheckTemporaryAnimator(*runtime);
        CheckReleaseConstructionFailure(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS unowned creation, rooted subtree adoption, frame-tail collection, lifecycle gating and deferred reattachment\n";
        return 0;
    } catch(const std::exception& exception) {std::cerr<<"FAIL "<<exception.what()<<'\n';delete runtime;return 1;}
}
