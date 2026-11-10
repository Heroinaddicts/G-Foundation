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
    ~SessionScript() override {++Destroyed;}
    float Value=1;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        Check(_Runtime==runtime,"Script construction received a different Runtime from its lifecycle");
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
private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<SessionScript>;
    explicit SessionScript(GameSproutRuntime::IGameSproutRuntime* runtime) : _Runtime(runtime) {++Constructed;}
    GameSproutRuntime::IGameSproutRuntime* const _Runtime;
};
static_assert(!std::is_constructible_v<SessionScript, GameSproutRuntime::IGameSproutRuntime*>);
class SimulationScript final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit SimulationScript(GameSproutRuntime::IGameSproutRuntime*) {}
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
void CheckParkedAuthoringScenes(GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::string error;
    auto* first=runtime.CreateScene("First authoring scene");
    auto* second=runtime.CreateScene("Second authoring scene");
    auto* play=runtime.CreateScene("Gameplay scene");
    Check(first&&second&&play,"Cannot create authoring-scene parking fixture");
    auto* authored=first->CreateGameObject("Authoring object");
    auto* playing=play->CreateGameObject("Gameplay object");
    Check(authored&&playing,"Cannot create authoring-scene parking objects");
    Check(runtime.BindSceneObjectIdentity(authored,1,error),error);
    Check(runtime.AddScriptComponent(authored,"simulation-fixture",{"simulation",1},0,error)&&
          runtime.AddScriptComponent(playing,"simulation-fixture",{"simulation",1},0,error),error);
    const auto authorID=authored->GetInstanceID();
    first->SetSimulationEnabled(false);
    Check(!runtime.ParkAuthoringScenes({first->GetInstanceID(),first->GetInstanceID()},error)&&
        runtime.GetSceneCount()==3&&runtime.QueryGameObject(authorID)==authored,"Duplicate parking changed Runtime state");
    Check(runtime.ParkAuthoringScenes({second->GetInstanceID(),first->GetInstanceID()},error),error);
    Check(runtime.GetSceneCount()==1&&runtime.GetScene(0)==play&&!runtime.QueryGameObject(authorID)&&
        !runtime.QuerySceneObjectByFileID(first,1)&&runtime.FindGameObjectWithTag("Untagged")==playing,
        "Authoring objects leaked into the gameplay lookup scope");
    Check(!first->CreateGameObject("Leaked mutation")&&!runtime.DestroyScene(first),"Parked scenes accepted a structural mutation");
    Check(runtime.BeginExecution(error),error);
    Check(playing->GetComponent<SimulationScript>()&&!authored->GetComponent<SimulationScript>()&&
        !runtime.RestoreAuthoringScenes(error),"Gameplay initialized a parked script or exposed a parked scene");
    Check(runtime.EndExecution(error)&&runtime.RestoreAuthoringScenes(error),error);
    Check(runtime.GetSceneCount()==3&&runtime.GetScene(0)==first&&runtime.GetScene(1)==second&&runtime.GetScene(2)==play&&
        runtime.QueryGameObject(authorID)==authored&&runtime.QuerySceneObjectByFileID(first,1)==authored&&
        !first->IsSimulationEnabled(),"Authoring restore changed original identities, order or simulation settings");
    Check(runtime.DestroyScene(play),"Cannot release the gameplay fixture");
    first->SetSimulationEnabled(true);
    Check(runtime.BeginExecution(error)&&authored->GetComponent<SimulationScript>()&&runtime.EndExecution(error),
        "Restored authoring script could not initialize after parking: "+error);
    Check(runtime.DestroyScene(second)&&runtime.DestroyScene(first),"Cannot release the authoring fixtures");
}

std::vector<std::string> ModuleCalls;
bool RejectModuleLaunch=false, ThrowModuleUpdate=false, ReleaseSawPeer=false;
int NextModuleInstance=0;
class IPhaseModule : public GameSproutRuntime::IModule {
public:
    virtual int InstanceNumber() const = 0;
};
class IOtherModule : public GameSproutRuntime::IModule {};

template<char Letter> class PhaseModule final : public IPhaseModule {
public:
    explicit PhaseModule(GameSproutRuntime::IGameSproutRuntime* runtime)
        : _Runtime(runtime), _Instance(++NextModuleInstance) {Record("Create");}
    ~PhaseModule() override {Record("Destroy");}
    int InstanceNumber() const override {return _Instance;}
protected:
    bool Initialize(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        Check(_Runtime==runtime,"Module construction received a different Runtime from its lifecycle");
        Check(_Runtime->FindModule<IPhaseModule>("A")&&_Runtime->FindModule<IPhaseModule>("B"),
            "Module peer lookup must be available before Initialize");
        Record("Initialize");
        return true;
    }
    bool Launch(GameSproutRuntime::IGameSproutRuntime*) override {
        Record("Launch");
        return Letter!='B'||!RejectModuleLaunch;
    }
    void LaunchFinished(GameSproutRuntime::IGameSproutRuntime*) override {Record("Ready");}
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {Record("Early");}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {Record("Fixed");}
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {
        Record("Update");
        if(Letter=='B'&&ThrowModuleUpdate)throw std::runtime_error("Rejected Update fixture");
    }
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {Record("Late");}
    void Release(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        if(Letter=='B')ReleaseSawPeer=runtime->FindModule("A")!=nullptr;
        Record("Release");
    }
private:
    static void Record(const char* callback) {ModuleCalls.push_back(std::string(callback)+Letter);}
    GameSproutRuntime::IGameSproutRuntime* const _Runtime;
    int _Instance;
};
template<char Letter> GameSproutRuntime::GameModuleRegistration PhaseRegistration() {
    return {std::string(1,Letter),std::string("fixture-")+Letter,__FILE__,
        [](GameSproutRuntime::IGameSproutRuntime* runtime)->GameSproutRuntime::IModule* {return new PhaseModule<Letter>(runtime);},
        [](GameSproutRuntime::IModule* module) {delete static_cast<PhaseModule<Letter>*>(module);}};
}
class ModuleEvents final : public GameSproutRuntime::IRuntimeListener {
public:
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::OperationFailed&&event.Name=="B")Error=event.Error;
    }
    std::string Error;
};
void CheckGameModules(GameSproutRuntime::IGameSproutRuntime& runtime) {
    const auto a=PhaseRegistration<'A'>(),b=PhaseRegistration<'B'>();
    std::string error;
    Check(runtime.ReplaceGameLogicTypes({}, {}, {b,a}, error),error);
    Check(!runtime.ReplaceGameLogicTypes({}, {}, {a,a}, error),"Duplicate modules changed the catalog");
    Check(!runtime.ReplaceGameLogicTypes({"missing-script"}, {}, {}, error),"Missing script accepted during catalog replacement");
    Check(runtime.ReplaceScriptTypes({}, {}, error),error);
    runtime.AdvanceFrame(.1f,1,.02f);
    Check(ModuleCalls.empty()&&!runtime.FindModule("A"),"Editor preview instantiated modules");
    Check(runtime.BeginExecution(error),error);
    const std::vector<std::string> startup={"CreateA","CreateB","InitializeA","InitializeB","LaunchA","LaunchB","ReadyA","ReadyB"};
    Check(ModuleCalls==startup,"Module startup skipped or mixed lifecycle phases");
    auto* first=runtime.FindModule<IPhaseModule>("A");
    Check(first&&!runtime.FindModule<IOtherModule>("A")&&!runtime.FindModule("missing"),"Typed module lookup accepted the wrong module");
    const auto firstInstance=first->InstanceNumber();
    Check(!runtime.ReplaceGameLogicTypes({}, {}, {}, error),"Live module catalog was replaced");
    runtime.ProcessPendingEvents();
    Check(ModuleCalls==startup,"Paused event delivery ticked modules");
    ModuleCalls.clear();
    runtime.AdvanceFrame(.1f,2,.02f);
    runtime.FixedUpdate(.02f);
    Check(ModuleCalls==std::vector<std::string>{"EarlyA","EarlyB","FixedA","FixedB","FixedA","FixedB",
        "UpdateA","UpdateB","LateA","LateB","FixedA","FixedB"},"Module frame phases were incomplete or out of order");
    ModuleCalls.clear();
    Check(runtime.EndExecution(error),error);
    const std::vector<std::string> stop={"ReleaseB","DestroyB","ReleaseA","DestroyA"};
    Check(ModuleCalls==stop&&ReleaseSawPeer&&!runtime.FindModule("A"),"Stop did not release modules in reverse order");

    ModuleCalls.clear();
    RejectModuleLaunch=true;
    Check(!runtime.BeginExecution(error)&&error.find("Game module B Launch")!=std::string::npos,
        "Module launch failure was not reported");
    std::vector<std::string> failed={"CreateA","CreateB","InitializeA","InitializeB","LaunchA","LaunchB"};
    failed.insert(failed.end(),stop.begin(),stop.end());
    Check(ModuleCalls==failed&&!runtime.IsScriptExecutionEnabled()&&!runtime.FindModule("A"),
        "Failed startup leaked module state or launched callbacks");
    RejectModuleLaunch=false;
    Check(runtime.BeginExecution(error),error);
    first=runtime.FindModule<IPhaseModule>("A");
    Check(first&&first->InstanceNumber()!=firstInstance,"Replay reused the previous module instance");
    ModuleEvents events;
    Check(runtime.RegisterListener(&events),"Cannot listen for module callback failure");
    ModuleCalls.clear();
    ThrowModuleUpdate=true;
    runtime.Update(.1f);
    ThrowModuleUpdate=false;
    runtime.Update(.1f);
    runtime.UnregisterListener(&events);
    Check(ModuleCalls==std::vector<std::string>{"EarlyA","EarlyB","UpdateA","UpdateB","LateA","EarlyA","UpdateA","LateA"}
        &&events.Error.find("Rejected Update fixture")!=std::string::npos,"Module tick failure was not isolated or reported");
    Check(runtime.EndExecution(error)&&runtime.ReplaceGameLogicTypes({}, {}, {}, error),error);
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
        CheckParkedAuthoringScenes(*runtime);
        CheckGameModules(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS staged native startup, restored peer fields, paused events, module phases and failed-begin retry\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}
