#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
const GameSproutRuntime::ResourceReference MeshReference{"0123456789abcdef0123456789abcdef",7};
struct Counters {
    int RootCreated=0,RootInitialized=0,ChildCreated=0,ChildInitialized=0;
    int LateCreated=0,LateInitialized=0,Updated=0,Fixed=0,Released=0,Destroyed=0;
    int VictimInitialized=0,VictimReleased=0,RemovingInitialized=0,SelfDestroyedInitialized=0;
    int FailureInitialized=0,FailureReleased=0,FailureDestroyed=0;
    int RestartConstructed=0;
    std::vector<int> RestartCreated,RestartInitialized,RestartUpdated,RestartFixed,RestartReleased,RestartDestroyed;
    float ExpectedValue=37;
    bool FailInitialization=false,OpenConstructionBeforeFailure=false,RestartInCreate=false,Restarted=false;
    std::uint64_t ChildID=0,LateID=0,SelfDestroyedID=0;
};
Counters Counts;

class LateReady final : public GameSproutRuntime::ScriptBehaviour {
public:
    float Value=1;
    ~LateReady() override {++Counts.Destroyed;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.LateCreated;}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_GameObject->Parent()!=nullptr,"Next-batch initialization saw an unattached object");
        Check(Value==52,"Next-batch initialization ran before the callback configured its fields");
        Check(_GameObject->GetComponent<GameSproutRuntime::IImageComponent>()!=nullptr,
              "Next-batch initialization ran before all components were added");
        ++Counts.LateInitialized;
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Updated;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.Released;}
};

class ReadyChild final : public GameSproutRuntime::ScriptBehaviour {
public:
    float Value=1;
    bool SpawnLate=false;
    ~ReadyChild() override {++Counts.Destroyed;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.ChildCreated;}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        Check(_GameObject->Parent()!=nullptr,"OnInitialized ran before parent attachment");
        const auto* mesh=_GameObject->GetComponent<GameSproutRuntime::IMeshComponent>();
        Check(mesh&&mesh->GetMesh()==MeshReference,"OnInitialized did not see the completed component/resource configuration");
        Check(Value==Counts.ExpectedValue,"OnInitialized ran before authored/configured fields were ready");
        ++Counts.ChildInitialized;
        if(!SpawnLate)return;
        auto* late=runtime->CreateGameObject("Created during OnInitialized",_GameObject);
        Check(late,"Cannot create a node during OnInitialized");
        Counts.LateID=late->GetInstanceID();
        auto* behaviour=late->AddComponent<LateReady>();
        Check(behaviour,"Cannot configure a native component in the next initialization batch");
        behaviour->Value=52;
        Check(late->AddComponent<GameSproutRuntime::IImageComponent>(),"Cannot complete the next initialization batch");
        Check(Counts.LateInitialized==0,"A nested OnInitialized callback ran before its creator returned");
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Updated;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Counts.Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.Released;}
};

class ConstructionRoot final : public GameSproutRuntime::ScriptBehaviour {
public:
    ~ConstructionRoot() override {++Counts.Destroyed;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        ++Counts.RootCreated;
        auto* child=runtime->CreateGameObject("Created during OnCreate",_GameObject);
        Check(child,"Cannot construct child during OnCreate");
        Counts.ChildID=child->GetInstanceID();
        auto* mesh=child->AddComponent<GameSproutRuntime::IMeshComponent>();
        Check(mesh,"Cannot configure OnCreate child Mesh");mesh->SetMesh(MeshReference);
        auto* behaviour=child->AddComponent<ReadyChild>();
        Check(behaviour,"Cannot configure OnCreate child script");
        behaviour->Value=Counts.ExpectedValue;behaviour->SpawnLate=true;
        Check(Counts.ChildInitialized==0,"OnInitialized reentered an unfinished OnCreate callback");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        auto* child=runtime->QueryGameObject(Counts.ChildID);
        Check(child&&child->Parent()==_GameObject&&_GameObject->FindChild("Created during OnCreate")==child,
              "Initial OnInitialized saw an incomplete hierarchy from OnCreate");
        Check(Counts.ChildCreated==1,"Initial OnInitialized ran before OnCreate completed for added components");
        auto* behaviour=child->GetComponent<ReadyChild>();
        Check(behaviour&&behaviour->Value==Counts.ExpectedValue,"Initial OnInitialized could not inspect its configured child component");
        ++Counts.RootInitialized;
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.Released;}
};

class PendingVictim final : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.VictimInitialized;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.VictimReleased;}
};
class RemovesPending final : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_GameObject->RemoveComponent<PendingVictim>(),"Cannot remove another pending component");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.RemovingInitialized;}
};
class DestroysOnInitialized final : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        ++Counts.SelfDestroyedInitialized;
        Check(_GameObject->GetScene()->DestroyGameObject(_GameObject),"Initialization could not request owner destruction");
        Check(_GameObject->GetInstanceID()==Counts.SelfDestroyedID,"Initialization destroyed its owner on the callback stack");
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {throw std::runtime_error("Destroyed component was updated");}
};
class FailsOnInitialized final : public GameSproutRuntime::ScriptBehaviour {
public:
    ~FailsOnInitialized() override {++Counts.FailureDestroyed;}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        ++Counts.FailureInitialized;
        if(!Counts.FailInitialization)return;
        if(Counts.OpenConstructionBeforeFailure) {
            std::string error;
            Check(runtime->BeginConstruction(error),error);
        }
        throw std::runtime_error("Rejected OnInitialized fixture");
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Counts.FailureReleased;}
};
class RestartsInCallback final : public GameSproutRuntime::ScriptBehaviour {
public:
    ~RestartsInCallback() override {Counts.RestartDestroyed.push_back(_Serial);}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Counts.RestartCreated.push_back(_Serial);
        if(Counts.RestartInCreate) RestartOnce();
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Counts.RestartInitialized.push_back(_Serial);
        if(!Counts.RestartInCreate) RestartOnce();
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {Counts.RestartUpdated.push_back(_Serial);}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {Counts.RestartFixed.push_back(_Serial);}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {Counts.RestartReleased.push_back(_Serial);}
private:
    void RestartOnce() {
        if(Counts.Restarted)return;
        Counts.Restarted=true;
        auto* wrapper=_GameObject->GetComponent<GameSproutRuntime::IScriptComponent>();
        Check(wrapper,"Cannot find the authored wrapper during its native callback");
        wrapper->SetEnabled(false);wrapper->SetEnabled(true);
        Check(Counts.RestartReleased.empty()&&Counts.RestartDestroyed.empty(),
              "Disable/re-enable destroyed the native instance while its callback was on the stack");
    }
    int _Serial=++Counts.RestartConstructed;
};

template<typename T>
void RegisterType(GameSproutRuntime::IGameSproutRuntime& runtime,const char* id,const char* name) {
    GameSproutRuntime::Internal::RegisterScriptClass<T>(id,name,__FILE__,{id,1},{});
    std::string error;
    Check(runtime.RegisterScriptType(GameSproutRuntime::Internal::ScriptModuleTypes().back(),error),error);
}

void CheckStartupClosure(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};
    std::string error;
    Check(runtime.BeginConstruction(error),error);
    auto* scene=runtime.CreateScene("Completed startup graph");
    auto* root=runtime.CreateGameObject("Root",scene);
    Check(root,"Cannot create startup root");
    Check(runtime.AddScriptComponent(root,"initialized-root-fixture",{"initialized-root-fixture",1},0,error),error);
    Check(runtime.EndConstruction(error),error);
    Check(Counts.RootCreated==0&&Counts.RootInitialized==0,"Editing invoked gameplay initialization");
    Check(runtime.BeginExecution(error),error);
    Check(Counts.RootCreated==1&&Counts.RootInitialized==1&&Counts.ChildCreated==1&&Counts.ChildInitialized==1&&
          Counts.LateCreated==1&&Counts.LateInitialized==1,"BeginExecution did not drain all initialization batches");
    auto* child=runtime.QueryGameObject(Counts.ChildID);
    auto* late=runtime.QueryGameObject(Counts.LateID);
    Check(child&&late&&late->Parent()==child,"Next initialization batch did not commit its hierarchy");
    Check(Counts.Updated==0&&Counts.Fixed==0,"Initialization advanced gameplay before startup finished");
    runtime.Update(.01f);runtime.FixedUpdate(.01f);runtime.ProcessPendingEvents();
    Check(Counts.ChildInitialized==1&&Counts.LateInitialized==1&&Counts.Updated==2&&Counts.Fixed==2,
          "Ready components were initialized twice or did not receive their first normal tick");
    Check(runtime.EndExecution(error),error);
    Check(Counts.Released==3&&Counts.Destroyed==3,"Initialized startup instances were not released exactly once");
    Check(runtime.DestroyScene(scene),"Cannot destroy startup fixture");
}

void CheckExplicitConstruction(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};Counts.ExpectedValue=64;
    std::string error;
    auto* scene=runtime.CreateScene("Dynamic construction");
    auto* object=runtime.CreateGameObject("Configured object",scene);
    Check(runtime.BeginExecution(error),error);
    Check(runtime.BeginConstruction(error)&&runtime.BeginConstruction(error),error);
    auto* behaviour=object->AddComponent<ReadyChild>();
    Check(behaviour,"Cannot add a native script to an existing ready object");
    Check(Counts.ChildInitialized==0,"AddComponent initialized the script before the caller configured it");
    behaviour->Value=64;
    auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();
    Check(mesh,"Cannot configure a component after adding its script");mesh->SetMesh(MeshReference);
    runtime.Update(.1f);runtime.FixedUpdate(.1f);runtime.ProcessPendingEvents();
    Check(Counts.ChildInitialized==0&&Counts.Updated==0&&Counts.Fixed==0,"Open construction leaked into initialization or gameplay");
    Check(runtime.EndConstruction(error),error);
    Check(Counts.ChildInitialized==0,"Nested construction initialized before its outer scope ended");
    Check(runtime.EndConstruction(error),error);
    Check(Counts.ChildCreated==1&&Counts.ChildInitialized==1&&Counts.Updated==0,"Construction commit did not initialize exactly once without updating");
    Check(!runtime.EndConstruction(error),"Unmatched EndConstruction was accepted");
    runtime.Update(.1f);runtime.FixedUpdate(.1f);
    Check(Counts.ChildInitialized==1&&Counts.Updated==1&&Counts.Fixed==1,"First tick after construction repeated initialization");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),"Explicit construction fixture cleanup failed");
}

void CheckImplicitConstruction(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};Counts.ExpectedValue=81;
    std::string error;
    auto* scene=runtime.CreateScene("Implicit construction");
    auto* object=runtime.CreateGameObject("Configured before first tick",scene);
    Check(runtime.BeginExecution(error),error);
    auto* behaviour=object->AddComponent<ReadyChild>();
    Check(behaviour,"Cannot add implicitly pending script");behaviour->Value=81;
    auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();
    Check(mesh,"Cannot configure implicitly pending Mesh");mesh->SetMesh(MeshReference);
    runtime.Update(0);runtime.FixedUpdate(0);runtime.ProcessPendingEvents();
    Check(Counts.ChildInitialized==0&&Counts.Updated==0&&Counts.Fixed==0,"Paused/event-only processing invoked pending initialization");
    runtime.FixedUpdate(.02f);
    Check(Counts.ChildCreated==1&&Counts.ChildInitialized==1&&Counts.Fixed==1&&Counts.Updated==0,
          "Positive fixed tick did not finish initialization before FixedUpdate");
    runtime.Update(.01f);
    Check(Counts.ChildInitialized==1&&Counts.Updated==1,"Variable tick repeated initialization after the first fixed tick");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),"Implicit construction fixture cleanup failed");
}

void CheckRemovalAndDestruction(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};
    std::string error;
    auto* scene=runtime.CreateScene("Pending removal");
    auto* object=runtime.CreateGameObject("Owner",scene);
    Check(runtime.BeginExecution(error)&&runtime.BeginConstruction(error),error);
    Check(object->AddComponent<PendingVictim>(),"Cannot add pending victim");
    Check(object->AddComponent<RemovesPending>(),"Cannot add removing component");
    Check(runtime.EndConstruction(error),error);
    Check(!object->GetComponent<PendingVictim>()&&Counts.VictimInitialized==0&&Counts.VictimReleased==1&&Counts.RemovingInitialized==1,
          "A removed pending component was initialized, retained, or released twice");
    Check(runtime.BeginConstruction(error),error);
    auto* dying=runtime.CreateGameObject("Destroy during initialization",scene);
    Counts.SelfDestroyedID=dying->GetInstanceID();
    Check(dying->AddComponent<DestroysOnInitialized>(),"Cannot add destruction fixture");
    Check(runtime.EndConstruction(error),error);
    Check(Counts.SelfDestroyedInitialized==1&&!runtime.QueryGameObject(Counts.SelfDestroyedID),
          "Owner destruction was not committed after its initialization callback");
    runtime.Update(.01f);runtime.FixedUpdate(.01f);
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),"Removal/destruction fixture cleanup failed");
}

void CheckInitializationFailure(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};Counts.FailInitialization=true;
    std::string error;
    auto* scene=runtime.CreateScene("Initialization failure");
    auto* object=runtime.CreateGameObject("Failing owner",scene);
    Check(runtime.AddScriptComponent(object,"initialized-failure-fixture",{"initialized-failure-fixture",1},0,error),error);
    Check(!runtime.BeginExecution(error)&&error=="Rejected OnInitialized fixture","OnInitialized failure did not abort startup with its error");
    Check(!runtime.IsScriptExecutionEnabled()&&Counts.FailureInitialized==1&&Counts.FailureReleased==1&&Counts.FailureDestroyed==1,
          "OnInitialized failure leaked the native instance or left gameplay enabled");
    Counts.FailInitialization=false;
    Check(runtime.BeginExecution(error),error);
    Check(Counts.FailureInitialized==2,"A fresh session could not initialize after a failed startup");
    Check(runtime.EndExecution(error),error);
    Check(Counts.FailureReleased==2&&Counts.FailureDestroyed==2,"Retry after initialization failure did not release exactly once");
    Check(runtime.DestroyScene(scene),"Cannot destroy initialization-failure fixture");
}

void CheckReparentActivation(GameSproutRuntime::IGameSproutRuntime& runtime) {
    Counts={};Counts.ExpectedValue=1;
    std::string error;
    auto* scene=runtime.CreateScene("Activation by reparenting");
    auto* inactive=runtime.CreateGameObject("Inactive parent",scene);
    auto* active=runtime.CreateGameObject("Active parent",scene);
    inactive->SetActive(false);
    auto* branch=runtime.CreateGameObject("Moved branch",inactive);
    auto* leaf=runtime.CreateGameObject("Pending descendant",branch);
    auto* mesh=leaf->AddComponent<GameSproutRuntime::IMeshComponent>();
    Check(mesh,"Cannot configure the inactive descendant Mesh");mesh->SetMesh(MeshReference);
    Check(runtime.AddScriptComponent(leaf,"initialized-child-fixture",{"initialized-child-fixture",1},0,error),error);
    Check(runtime.BeginExecution(error),error);
    runtime.Update(.01f);runtime.FixedUpdate(.01f);
    Check(!leaf->IsActiveInHierarchy()&&Counts.ChildCreated==0&&Counts.ChildInitialized==0&&Counts.Updated==0&&Counts.Fixed==0,
          "An inactive descendant executed before hierarchy activation");
    Check(active->AddChild(branch,false),"Cannot move the inactive branch under an active parent");
    Check(leaf->IsActiveInHierarchy(),"Reparenting did not activate the descendant hierarchy");
    runtime.Update(.01f);runtime.FixedUpdate(.01f);
    Check(Counts.ChildCreated==1&&Counts.ChildInitialized==1&&Counts.Updated==1&&Counts.Fixed==1,
          "Hierarchy activation by reparenting did not initialize its pending descendant before gameplay");
    runtime.Update(.01f);
    Check(Counts.ChildCreated==1&&Counts.ChildInitialized==1&&Counts.Updated==2,
          "Reparent-activated component was initialized again on its next tick");
    Check(runtime.EndExecution(error)&&runtime.DestroyScene(scene),"Reparent-activation fixture cleanup failed");
}

void CheckRestartDuringInitialization(GameSproutRuntime::IGameSproutRuntime& runtime) {
    for(bool duringCreate:{true,false}) {
        Counts={};Counts.RestartInCreate=duringCreate;
        std::string error;
        auto* scene=runtime.CreateScene(duringCreate?"Restart during OnCreate":"Restart during OnInitialized");
        auto* object=runtime.CreateGameObject("Restarted component",scene);
        Check(runtime.AddScriptComponent(object,"initialized-restart-fixture",{"initialized-restart-fixture",1},0,error),error);
        Check(runtime.BeginExecution(error),error);
        Check(Counts.RestartConstructed==2&&Counts.RestartCreated==std::vector<int>{1,2},
              "A callback restart did not create each native instance exactly once");
        const std::vector<int> expectedInitialized=duringCreate?std::vector<int>{2}:std::vector<int>{1,2};
        Check(Counts.RestartInitialized==expectedInitialized,
              "A callback restart skipped the replacement's OnInitialized or initialized an instance twice");
        Check(Counts.RestartReleased==std::vector<int>{1}&&Counts.RestartDestroyed==std::vector<int>{1},
              "The restarted native instance was not released/destroyed exactly once");
        Check(Counts.RestartUpdated.empty()&&Counts.RestartFixed.empty(),"Restarting initialization advanced gameplay");
        runtime.Update(.01f);runtime.FixedUpdate(.01f);runtime.Update(.01f);
        Check(Counts.RestartUpdated==std::vector<int>{2,2}&&Counts.RestartFixed==std::vector<int>{2},
              "The replacement native instance remained uninitialized or a released instance received gameplay callbacks");
        Check(Counts.RestartInitialized==expectedInitialized,"Ordinary gameplay repeated OnInitialized after a callback restart");
        Check(runtime.EndExecution(error),error);
        Check(Counts.RestartReleased==std::vector<int>{1,2}&&Counts.RestartDestroyed==std::vector<int>{1,2},
              "Stopping a restarted component did not release/destroy both instances exactly once");
        Check(runtime.DestroyScene(scene),"Cannot destroy callback-restart fixture");
    }
}

void CheckConstructionFailureRecovery(GameSproutRuntime::IGameSproutRuntime& runtime) {
    for(bool alreadyExecuting:{false,true}) {
        Counts={};Counts.FailInitialization=true;Counts.OpenConstructionBeforeFailure=true;
        std::string error;
        auto* scene=runtime.CreateScene(alreadyExecuting?"Failed host construction":"Failed startup construction");
        auto* failing=runtime.CreateGameObject("Leaves a construction scope open",scene);
        if(alreadyExecuting) {
            Check(runtime.BeginExecution(error),error);
            Check(runtime.BeginConstruction(error),error);
        }
        Check(runtime.AddScriptComponent(failing,"initialized-failure-fixture",{"initialized-failure-fixture",1},0,error),error);
        const bool committed=alreadyExecuting?runtime.EndConstruction(error):runtime.BeginExecution(error);
        Check(!committed&&error=="Rejected OnInitialized fixture",
              "An initialization exception with an open construction scope did not report its failure");
        Check(runtime.IsScriptExecutionEnabled()==alreadyExecuting&&Counts.FailureInitialized==1&&
              Counts.FailureReleased==1&&Counts.FailureDestroyed==1,
              "Failed construction left the wrong execution state or leaked/double-released the failed instance");
        Check(!runtime.EndConstruction(error),"Failed initialization retained an unmatched construction scope");

        Counts.FailInitialization=false;Counts.OpenConstructionBeforeFailure=false;
        Check(runtime.BeginConstruction(error),error);
        auto* recovered=runtime.CreateGameObject("New component after failed construction",scene);
        Check(recovered,"A failed initialization blocked later object construction");
        Check(runtime.AddScriptComponent(recovered,"initialized-failure-fixture",{"initialized-failure-fixture",1},0,error),error);
        Check(runtime.EndConstruction(error),error);
        if(!alreadyExecuting)Check(runtime.BeginExecution(error),error);
        const int expectedInstances=alreadyExecuting?2:3;
        Check(Counts.FailureInitialized==expectedInstances,
              "A hidden construction depth blocked later component initialization or session restart");
        Check(runtime.EndExecution(error),error);
        Check(!runtime.IsScriptExecutionEnabled()&&Counts.FailureReleased==expectedInstances&&Counts.FailureDestroyed==expectedInstances,
              "Execution could not stop cleanly after failed construction recovery");
        Check(runtime.DestroyScene(scene),"Failed-construction recovery fixture cleanup failed");
    }
}
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try {
        Check(runtime,"Cannot load Runtime");
        RegisterType<ConstructionRoot>(*runtime,"initialized-root-fixture","ConstructionRoot");
        RegisterType<ReadyChild>(*runtime,"initialized-child-fixture","ReadyChild");
        RegisterType<LateReady>(*runtime,"initialized-late-fixture","LateReady");
        RegisterType<PendingVictim>(*runtime,"initialized-victim-fixture","PendingVictim");
        RegisterType<RemovesPending>(*runtime,"initialized-remover-fixture","RemovesPending");
        RegisterType<DestroysOnInitialized>(*runtime,"initialized-destroy-fixture","DestroysOnInitialized");
        RegisterType<FailsOnInitialized>(*runtime,"initialized-failure-fixture","FailsOnInitialized");
        RegisterType<RestartsInCallback>(*runtime,"initialized-restart-fixture","RestartsInCallback");
        CheckStartupClosure(*runtime);
        CheckExplicitConstruction(*runtime);
        CheckImplicitConstruction(*runtime);
        CheckRemovalAndDestruction(*runtime);
        CheckInitializationFailure(*runtime);
        CheckReparentActivation(*runtime);
        CheckRestartDuringInitialization(*runtime);
        CheckConstructionFailureRecovery(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS completed initialization graph, construction boundaries, pending removal/destruction and failed-start retry\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}
