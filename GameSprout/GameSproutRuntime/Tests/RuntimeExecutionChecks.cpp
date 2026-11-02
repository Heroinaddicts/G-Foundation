#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error) {if(!value)throw std::runtime_error(error);}
int Constructed=0,Created=0,Initialized=0,Released=0,Destroyed=0,Updated=0,Fixed=0;
bool FailCreate=false;
GameSproutRuntime::IGameObject* Peer=nullptr;
class SessionScript final : public GameSproutRuntime::ScriptBehaviour {
public:
    SessionScript(){++Constructed;}
    ~SessionScript() override {++Destroyed;}
    float Value=1;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(Constructed==2,"OnCreate ran before all native instances were constructed");
        auto* peer=Peer->GetComponent<SessionScript>();
        Check(peer&&peer->Value==9,"OnCreate could not see the restored peer field");
        ++Created;
        if(FailCreate)throw std::runtime_error("Rejected startup fixture");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(Created==2,"OnInitialized ran before all OnCreate callbacks completed");++Initialized;
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Updated;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Released;}
};
class SimulationScript final : public GameSproutRuntime::ScriptBehaviour {
public:
    int Updates=0,FixedTicks=0;
    void Update(GameSproutRuntime::IGameSproutRuntime*,float delta) override {
        ++Updates;auto* transform=_GameObject->GetTransform();auto position=transform->GetLocalPosition();position.X+=delta;transform->SetLocalPosition(position);
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float delta) override {
        ++FixedTicks;auto* transform=_GameObject->GetTransform();auto position=transform->GetLocalPosition();position.Y+=delta;transform->SetLocalPosition(position);
    }
};
void Reset(){Constructed=Created=Initialized=Released=Destroyed=Updated=Fixed=0;}
void CheckSceneSimulation(GameSproutRuntime::IGameSproutRuntime& runtime) {
    GameSproutRuntime::Internal::RegisterScriptClass<SimulationScript>("simulation-fixture","SimulationScript",__FILE__,{"simulation",1},{});
    std::string error;bool registered=false;
    for(const auto& registration:GameSproutRuntime::Internal::ScriptModuleTypes())if(registration.Info.TypeID=="simulation-fixture")
        registered=runtime.RegisterScriptType(registration,error);
    Check(registered,error);
    auto* suspended=runtime.CreateScene("Suspended host world");auto* preview=runtime.CreateScene("Another host world");
    Check(suspended&&preview&&suspended->IsSimulationEnabled()&&preview->IsSimulationEnabled(),"Scene simulation must default to enabled");
    auto* first=suspended->CreateGameObject("Frozen component");auto* second=preview->CreateGameObject("Ticking component");
    for(auto* object:{first,second})Check(runtime.AddScriptComponent(object,"simulation-fixture",{"simulation",1},0,error),error);
    auto* camera=first->AddComponent<GameSproutRuntime::ICameraComponent>();Check(camera&&suspended->SetMainCamera(camera),"Cannot create a renderable frozen world");
    Check(runtime.BeginExecution(error),error);
    auto* frozen=first->GetComponent<SimulationScript>();auto* ticking=second->GetComponent<SimulationScript>();Check(frozen&&ticking,"Missing native simulation fixture");
    suspended->SetSimulationEnabled(false);runtime.Update(.25f);runtime.FixedUpdate(.25f);
    Check(frozen->Updates==0&&frozen->FixedTicks==0&&ticking->Updates==1&&ticking->FixedTicks==1,
        "Disabling one scene did not isolate variable/fixed component simulation");
    Check(first->GetTransform()->GetLocalPosition().X==0&&first->GetTransform()->GetLocalPosition().Y==0&&
          second->GetTransform()->GetLocalPosition().X==.25f&&second->GetTransform()->GetLocalPosition().Y==.25f,
        "Frozen scene changed its variable/fixed simulation data");
    Check(suspended->IsActiveInHierarchy()&&first->IsActiveInHierarchy()&&suspended->GetMainCamera()==camera,
        "Simulation suspension disabled the rendered hierarchy or lost its camera");
    suspended->SetSimulationEnabled(true);runtime.Update(.25f);runtime.FixedUpdate(.25f);
    Check(frozen->Updates==1&&frozen->FixedTicks==1&&ticking->Updates==2&&ticking->FixedTicks==2,
        "Restoring scene simulation did not resume both component tick paths");
    Check(runtime.EndExecution(error),error);Check(runtime.DestroyScene(suspended)&&runtime.DestroyScene(preview),"Simulation scene cleanup failed");
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try{
        Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<SessionScript>("execution-session-fixture","SessionScript",__FILE__,{"fixture",1},
            {GameSproutRuntime::Internal::MakeField<SessionScript>("value","Value",&SessionScript::Value,1.f)});
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().front();
        std::string error;Check(runtime->RegisterScriptType(registration,error),error);
        auto* scene=runtime->CreateScene("Session");
        for(int index=0;index<2;++index){
            auto* object=scene->CreateGameObject("Script");Peer=object;
            auto* script=runtime->AddScriptComponent(object,registration.Info.TypeID,registration.Source,0,error);
            Check(script&&script->SetField("value",GameSproutRuntime::Internal::ScriptValueOf(9.f),error),error);
        }
        runtime->Update(1);Check(Constructed==0,"Edit evaluation constructed game scripts");
        Check(runtime->BeginExecution(error),error);
        Check(Constructed==2&&Created==2&&Initialized==2&&Updated==0&&Fixed==0,"Begin did not finish startup phases before the first tick");
        Check(!runtime->BeginExecution(error),"Reentrant gameplay begin accepted");
        runtime->ProcessPendingEvents();runtime->ProcessPendingEvents();
        Check(Updated==0&&Fixed==0&&Destroyed==0,"Paused event processing evaluated or destroyed components");
        runtime->FixedUpdate(1.f/60);runtime->Update(1.f/60);
        Check(Updated==2&&Fixed==2,"Fixed and variable script dispatch failed");
        Check(runtime->EndExecution(error),error);
        Check(!runtime->IsScriptExecutionEnabled()&&Released==2&&Destroyed==2,"Stop did not release the complete native session");
        Check(runtime->GetScriptTypes().size()==1,"Stop discarded the code catalog");
        Reset();FailCreate=true;
        Check(!runtime->BeginExecution(error)&&error=="Rejected startup fixture","Startup error did not abort the gameplay session");
        Check(!runtime->IsScriptExecutionEnabled()&&Constructed==2&&Released==2&&Destroyed==2&&Initialized==0,"Failed startup leaked instances or ran initialization");
        Reset();FailCreate=false;Check(runtime->BeginExecution(error),error);Check(runtime->EndExecution(error),error);
        Check(Released==2&&Destroyed==2,"Retry after failed start did not recover");
        Check(runtime->DestroyScene(scene),"Cannot close startup fixture before scene simulation check");
        CheckSceneSimulation(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS staged native startup, restored peer fields, event-only pause, fixed dispatch, release and failed-begin retry\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
